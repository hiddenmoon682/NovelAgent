// SqliteStore 实现 — 建表迁移、WAL、事务包装与损坏自愈。

#include "storage/SqliteStore.h"

#include "project/ProjectIO.h"
#include "utils/FileUtils.h"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <sqlite-vec.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace storage {

namespace {

// sqlite-vec 在 SQLITE_CORE 静态编译模式下不自带 auto-extension 钩子，
// 需要在进程启动时用 sqlite3_auto_extension 挂载 sqlite3_vec_init，
// 之后每个新建连接自动注册 vec0/vec_distance_* 等虚拟表与函数。
struct Vec0AutoRegister {
    Vec0AutoRegister() {
        sqlite3_auto_extension(reinterpret_cast<void (*)(void)>(sqlite3_vec_init));
    }
};
Vec0AutoRegister g_vec0_auto_register;

// 当前时间戳 "2026-08-17T03:15:00Z"（文件后缀安全）。
std::string nowTimestamp() {
    const auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", &tm);
    return buf;
}

// 校验 path 是否为合法 SQLite 库：读取头部 16 字节魔数 "SQLite format 3\0"。
// 文件不存在或读取不足 16 字节 → 非法（供损坏自愈判定使用）。
bool hasValidSqliteHeader(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char buf[16];
    f.read(buf, sizeof(buf));
    if (f.gcount() < 16) return false;
    // 显式长度 16：魔数本身含结尾 '\0'
    static const std::string kMagic("SQLite format 3", 16);
    return std::memcmp(buf, kMagic.data(), 16) == 0;
}

} // namespace

void SqliteStore::open(const std::string& db_path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) return;

    path_ = db_path;
    const std::string dir = utils::file::dirName(db_path);
    if (!dir.empty() && !utils::file::exists(dir)) {
        utils::file::createDirs(dir);
    }

    try {
        // 打开 SQLite 库：READWRITE 读写方式；CREATE 库文件不存在时自动创建；
        // FULLMUTEX 开启全线程串行模式，允许同一连接被多线程交替调用。
        db_ = std::make_unique<SQLite::Database>(
            db_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE | SQLite::OPEN_FULLMUTEX);
        // 建表迁移：设置 WAL/外键 PRAGMA，按需创建 sessions/messages/kv_store 等表。
        ensureSchema();
        // 从 kv_store 恢复向量维度缓存：重启后同维度调用 ensureVectorTable 短路不 DROP；
        // 新库或自愈重建时 kv 缺失 → 恢复为 0，保持按需建表。
        restoreVectorDimension();
    } catch (const std::exception& e) {
        spdlog::error("[SqliteStore] 打开库失败: {} - {}", db_path, e.what());
        db_.reset();
        // 损坏自愈：仅当文件存在且头部魔数非法（确实不是 SQLite 库）时才
        // 改名重建，避免 open/建表等临时失败误伤健康库。头部合法或文件
        // 缺失时只记错误，保持未打开。
        if (utils::file::exists(path_) && !hasValidSqliteHeader(path_)) {
            removeCorruptAndReopen();
        } else {
            spdlog::warn("[SqliteStore] 库未销毁，保持未打开: {}", path_);
        }
    }
    // 仅真正打开成功（含自愈重建成功）时打印成功日志；失败路径的
    // error/warn 日志已在上面记录，不要误报"已打开"。
    if (db_) {
        spdlog::info("[SqliteStore] 已打开数据库: {}", db_path);
    }
}

void SqliteStore::close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return;
    // 显式 checkpoint 不是必须的：SQLite 在最后一个连接关闭时自动完成
    // WAL checkpoint；close 仅释放连接。
    db_.reset();
    vector_dimension_ = 0;
    spdlog::debug("[SqliteStore] 已关闭数据库");
}

void SqliteStore::exec(const std::string& sql)
{
    db_->exec(sql);
}

std::string SqliteStore::getKV(const std::string& key)
{
    SQLite::Statement stmt(*db_, "SELECT value FROM kv_store WHERE key = ?");
    stmt.bind(1, key);
    if (!stmt.executeStep()) return {};
    return stmt.getColumn(0).getString();
}

void SqliteStore::setKV(const std::string& key, const std::string& value)
{
    SQLite::Statement stmt(*db_,
        "INSERT INTO kv_store (key, value) VALUES (?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    stmt.bind(1, key);
    stmt.bind(2, value);
    stmt.exec();
}

void SqliteStore::ensureVectorTable(int dimension)
{
    if (!db_ || dimension <= 0) return;
    if (vector_dimension_ == dimension) return;

    // DROP 旧表（维度不同或首次建表）：虚拟表不支持 ALTER 改维度，
    // 维度变更语义即"整库失效"（调用方已在指纹层清空清单）。
    if (vector_dimension_ > 0 && vector_dimension_ != dimension) {
        spdlog::warn("[SqliteStore] vec_chunks 维度变更 {} → {}，DROP 重建（全部向量将清空）",
                     vector_dimension_, dimension);
    }
    db_->exec("DROP TABLE IF EXISTS vec_chunks");
    // 不保存原始向量的 JSON 副本：vec0 内部存储格式属实现细节，不做依赖假设，
    // get() 仅返回 id 与 metadata，embedding 恒为空。
    const std::string ddl =
        "CREATE VIRTUAL TABLE vec_chunks USING vec0("
        "  chunk_id TEXT,"
        "  metadata TEXT,"
        "  embedding float[" + std::to_string(dimension) + "] distance_metric=cosine"
        ")";
    db_->exec(ddl);
    // 记录维度到 kv_store：重启后 open() 恢复缓存，同维度调用短路不 DROP 重建。
    // 先写 kv 成功再更新缓存：setKV 抛异常时保持缓存不变（异常向上传播，
    // 调用方事务回滚表创建，状态自洽），避免缓存与 kv 失配导致重启后误 DROP。
    setKV("vector_dimension", std::to_string(dimension));
    vector_dimension_ = dimension;
    spdlog::info("[SqliteStore] vec_chunks 已创建 (维度 {})", dimension);
}

void SqliteStore::resetVectorTable()
{
    db_->exec("DROP TABLE IF EXISTS vec_chunks");
    // 同步清空 kv 中的维度记录：仅清零内存缓存会在重启时由
    // restoreVectorDimension 恢复旧维度，导致同维度 ensureVectorTable
    // 短路不重建表，后续 INSERT 落空（no such table）。
    db_->exec("DELETE FROM kv_store WHERE key = 'vector_dimension'");
    vector_dimension_ = 0;
}

void SqliteStore::ensureSchema()
{
    // 以下 DDL 全部幂等（IF NOT EXISTS），open() 与损坏自愈重建共用本函数。

    // WAL 日志模式：读写互不阻塞、崩溃后自动恢复；该属性为库级持久设置，
    // 每次打开连接显式设置一次，可覆盖旧库可能残留的非 WAL 状态。
    db_->exec("PRAGMA journal_mode=WAL");
    // SQLite 外键约束默认关闭且“每连接”生效，必须显式开启；
    // 否则删除 sessions 时，messages/index_chunks 的 CASCADE 清理不会触发。
    db_->exec("PRAGMA foreign_keys=ON");

    // 会话主表：每条会话一行，messages/message_history 均以 session_id 外键关联到此表。
    //   id         会话唯一 ID（主键）
    //   title      会话标题：首次保存时从首条 user 消息自动提取，空串 = 未命名
    //   created_at 创建时间（UTC 时间戳文本）
    //   updated_at 最后更新时间，会话列表按此倒序排列
    //   archived   归档标记：1=永久封存（数据保留、列表不可见），save() 刻意不复活
    db_->exec(
        "CREATE TABLE IF NOT EXISTS sessions ("
        " id TEXT PRIMARY KEY,"
        " title TEXT NOT NULL DEFAULT '',"
        " created_at TEXT NOT NULL,"
        " updated_at TEXT NOT NULL,"
        " archived INTEGER NOT NULL DEFAULT 0"
        ")");

    // 快照层：save() 事务内 DELETE + 重插全量覆盖（对应原 <id>.json），始终是最新会话状态。
    //   session_id        所属会话（外键 → sessions.id，会话删除时级联清理）
    //   seq               会话内序号：从 1 递增，与 session_id 组成主键，回放按此排序
    //   role              角色：user/assistant/tool（system 不入库，启动时重新组装）
    //   content           消息正文
    //   tool_calls        assistant 消息携带的工具调用 JSON 数组（无则 NULL）
    //   tool_call_id      工具结果消息关联的调用 ID（仅 tool 角色，用于结果↔调用配对）
    //   reasoning_content DeepSeek thinking 的推理过程（仅工具调用循环内回显）
    //   preserved         pin 标记：压缩/截断时优先保留（自动 pin 上限 12 条，手动 pin 不受限）
    //   is_control        控制消息占位（如取消提示）：非真实对话，UI 据此过滤/特殊渲染
    db_->exec(
        "CREATE TABLE IF NOT EXISTS messages ("
        " session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,"
        " seq INTEGER NOT NULL,"
        " role TEXT NOT NULL,"
        " content TEXT NOT NULL DEFAULT '',"
        " tool_calls TEXT,"
        " tool_call_id TEXT,"
        " reasoning_content TEXT,"
        " preserved INTEGER NOT NULL DEFAULT 0,"
        " is_control INTEGER NOT NULL DEFAULT 0,"
        " PRIMARY KEY (session_id, seq)"
        ")");

    // 完整历史层：append-only（对应原 <id>.history），每轮消息以 MAX(seq)+1 续号追加、
    // 只增不改，与快照层“重插覆盖”互补，保留完整消息序列供回滚与溯源；字段同 messages 表。
    db_->exec(
        "CREATE TABLE IF NOT EXISTS message_history ("
        " session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,"
        " seq INTEGER NOT NULL,"
        " role TEXT NOT NULL,"
        " content TEXT NOT NULL DEFAULT '',"
        " tool_calls TEXT,"
        " tool_call_id TEXT,"
        " reasoning_content TEXT,"
        " preserved INTEGER NOT NULL DEFAULT 0,"
        " is_control INTEGER NOT NULL DEFAULT 0,"
        " PRIMARY KEY (session_id, seq)"
        ")");

    // 检索索引来源清单（对应原 index_manifest.json）：
    // index_sources —— 每个被索引的来源文件一行：
    //   source_key   来源文件标识（主键）
    //   content_hash 文件内容哈希：哈希变 ⇔ 内容过期，需重建索引
    //   updated_at   索引更新时间（unix 秒）
    // index_chunks —— 来源切分出的块清单，来源删除时经外键 CASCADE 一并清理：
    //   source_key   所属来源（外键 → index_sources）
    //   chunk_id     向量块 ID（与 vec_chunks.chunk_id 对应）
    db_->exec(
        "CREATE TABLE IF NOT EXISTS index_sources ("
        " source_key TEXT PRIMARY KEY,"
        " content_hash TEXT NOT NULL,"
        " updated_at INTEGER NOT NULL"
        ")");
    db_->exec(
        "CREATE TABLE IF NOT EXISTS index_chunks ("
        " source_key TEXT NOT NULL REFERENCES index_sources(source_key) ON DELETE CASCADE,"
        " chunk_id TEXT NOT NULL,"
        " PRIMARY KEY (source_key, chunk_id)"
        ")");

    // 单行 KV 表：模型指纹、schema 版本、向量维度等键值项，覆盖写语义。
    //   key    项名称（主键）
    //   value  项值（任意文本）
    db_->exec(
        "CREATE TABLE IF NOT EXISTS kv_store ("
        " key TEXT PRIMARY KEY,"
        " value TEXT NOT NULL"
        ")");
}

void SqliteStore::restoreVectorDimension()
{
    vector_dimension_ = 0;
    try {
        const std::string v = getKV("vector_dimension");
        if (!v.empty()) vector_dimension_ = std::stoi(v);
    } catch (...) {
        // kv 缺失（新库/自愈重建）或写入值非法 → 保持 0，下次按需建表
        vector_dimension_ = 0;
    }
}

void SqliteStore::removeCorruptAndReopen()
{
    try {
        const std::string backup = path_ + ".corrupt-" + nowTimestamp();
        std::error_code ec;
        std::filesystem::rename(path_, backup, ec);
        if (ec) {
            spdlog::error("[SqliteStore] 损坏库改名失败: {} - {}", path_, ec.message());
            return;  // 保持未打开
        }
        spdlog::warn("[SqliteStore] 损坏库已改名备份: {}", backup);
        // WAL/SHM 伴生文件一并改名：旧 WAL 携带原库的随机盐（salt），与新库
        // 不匹配会导致重建后仍打开失败；伴生文件不存在时仅 warn，不中断自愈。
        for (const char* suffix : {"-wal", "-shm"}) {
            const std::string side_path = path_ + suffix;
            std::error_code sec;
            std::filesystem::rename(side_path, backup + suffix, sec);
            if (sec) {
                spdlog::warn("[SqliteStore] 伴生文件改名失败（可能不存在）: {} - {}",
                             side_path, sec.message());
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("[SqliteStore] 损坏库改名失败: {}", e.what());
        return;
    }

    try {
        db_ = std::make_unique<SQLite::Database>(
            path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE | SQLite::OPEN_FULLMUTEX);
        ensureSchema();
        restoreVectorDimension();
        spdlog::warn("[SqliteStore] 已重建空库: {}", path_);
    } catch (const std::exception& e) {
        db_.reset();
        spdlog::error("[SqliteStore] 重建空库失败，库保持关闭: {}", e.what());
    }
}

} // namespace storage