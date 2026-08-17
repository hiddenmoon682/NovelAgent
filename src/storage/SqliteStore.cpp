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
#include <filesystem>
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
        db_ = std::make_unique<SQLite::Database>(
            db_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE | SQLite::OPEN_FULLMUTEX);
        ensureSchema();
    } catch (const std::exception& e) {
        spdlog::error("[SqliteStore] 打开库失败（尝试损坏自愈）: {} - {}", db_path, e.what());
        db_.reset();
        removeCorruptAndReopen();
    }
    spdlog::info("[SqliteStore] 已打开数据库: {} ({} 张业务表)", db_path, 6);
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
    db_->exec("DROP TABLE IF EXISTS vec_chunks");
    // vec0 附加列：chunk_id/metadata/embedding_json 随行存储，可查询返回；
    // embedding_json 保存原始向量（JSON），避免依赖 vec0 内部存储格式。
    const std::string ddl =
        "CREATE VIRTUAL TABLE vec_chunks USING vec0("
        "  chunk_id TEXT,"
        "  metadata TEXT,"
        "  embedding_json TEXT,"
        "  embedding float[" + std::to_string(dimension) + "] distance_metric=cosine"
        ")";
    db_->exec(ddl);
    vector_dimension_ = dimension;
    spdlog::info("[SqliteStore] vec_chunks 已创建 (维度 {})", dimension);
}

void SqliteStore::ensureSchema()
{
    db_->exec("PRAGMA journal_mode=WAL");
    db_->exec("PRAGMA foreign_keys=ON");

    // 会话（archived=1 为归档态：数据保留、列表不可见）
    db_->exec(
        "CREATE TABLE IF NOT EXISTS sessions ("
        " id TEXT PRIMARY KEY,"
        " title TEXT NOT NULL DEFAULT '',"
        " created_at TEXT NOT NULL,"
        " updated_at TEXT NOT NULL,"
        " archived INTEGER NOT NULL DEFAULT 0"
        ")");

    // 快照层：save() 事务内 DELETE + 重插（对应原 <id>.json）
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

    // 完整历史层：append-only（对应原 <id>.history）
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

    // 索引清单（对应原 index_manifest.json）
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

    // 单行 KV（模型指纹、schema 版本等）
    db_->exec(
        "CREATE TABLE IF NOT EXISTS kv_store ("
        " key TEXT PRIMARY KEY,"
        " value TEXT NOT NULL"
        ")");
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
    } catch (const std::exception& e) {
        spdlog::error("[SqliteStore] 损坏库改名失败: {}", e.what());
        return;
    }

    try {
        db_ = std::make_unique<SQLite::Database>(
            path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE | SQLite::OPEN_FULLMUTEX);
        ensureSchema();
        spdlog::warn("[SqliteStore] 已重建空库: {}", path_);
    } catch (const std::exception& e) {
        db_.reset();
        spdlog::error("[SqliteStore] 重建空库失败，库保持关闭: {}", e.what());
    }
}

} // namespace storage