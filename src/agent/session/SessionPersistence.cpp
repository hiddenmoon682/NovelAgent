// SessionPersistence 实现 — SQLite 表读写（快照层 + 完整历史层）。

#include "agent/session/SessionPersistence.h"

#include "agent/context/Memory.h"
#include "llm/Message.h"
#include "storage/SqliteStore.h"

#include <SQLiteCpp/Statement.h>
#include <chrono>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace agent {

namespace {

// ── 消息 ↔ 行字段 ──

// system 消息一律不落盘（system prompt 由启动时重新组装）。
bool isSystem(const llm::Message& m) { return m.role == llm::MessageRole::System; }

std::string toolCallsToJson(const llm::Message& m)
{
    if (m.tool_calls.empty()) return {};
    nlohmann::json j = m.tool_calls;
    return j.dump();
}

std::vector<llm::ToolCall> parseToolCalls(const std::string& s)
{
    if (s.empty()) return {};
    try {
        return nlohmann::json::parse(s).get<std::vector<llm::ToolCall>>();
    } catch (...) {
        return {};
    }
}

// 绑定一条消息到 8 个业务列（seq, role, content, tool_calls, tool_call_id,
// reasoning_content, preserved, is_control）。调用方已先 bind(1, session_id)，
// 故业务列从占位符 2 起依次绑定（对应 9 列 INSERT）。
void bindMessageColumns(SQLite::Statement& stmt, const llm::Message& m, int seq)
{
    stmt.bind(2, seq);
    stmt.bind(3, llm::roleToString(m.role));
    stmt.bind(4, m.content);
    stmt.bind(5, toolCallsToJson(m));
    stmt.bind(6, m.tool_call_id);
    stmt.bind(7, m.reasoning_content);
    stmt.bind(8, m.preserved ? 1 : 0);
    stmt.bind(9, m.is_control ? 1 : 0);
}

// 从行列还原 Message（列序与 bindMessageColumns 一致）。
llm::Message rowToMessage(SQLite::Statement& stmt)
{
    llm::Message m;
    m.role = llm::roleFromString(stmt.getColumn(1).getString());
    m.content = stmt.getColumn(2).getString();
    m.tool_calls = parseToolCalls(stmt.getColumn(3).getString());
    m.tool_call_id = stmt.getColumn(4).getString();
    m.reasoning_content = stmt.getColumn(5).getString();
    m.preserved = stmt.getColumn(6).getInt() != 0;
    m.is_control = stmt.getColumn(7).getInt() != 0;
    return m;
}

// UTF-8 安全截断：最多保留 max_bytes 字节，退到字符边界，截断时追加省略号。
std::string utf8Truncate(const std::string& s, size_t max_bytes)
{
    if (s.size() <= max_bytes) return s;
    size_t end = max_bytes;
    while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) --end;
    return s.substr(0, end) + "…";
}

// 从 messages 提取首条 user 消息的首行作为会话标题；无 user 消息返回空。
std::string deriveTitle(const std::vector<llm::Message>& messages)
{
    for (const auto& m : messages) {
        if (m.role != llm::MessageRole::User) continue;
        std::string content = m.content;
        if (auto nl = content.find('\n'); nl != std::string::npos)
            content = content.substr(0, nl);
        return utf8Truncate(content, 30);
    }
    return {};
}

// "2026-08-27T15:16:00Z"（UTC，精确到秒，ProjectIO::nowTimestamp 产出）→ epoch 毫秒；
// 格式不符或日期非法返回 -1。物化会话时用库内真实时间覆盖 runtime 的排序时间戳
//（否则物化动作本身会把会话"最近活动时间"刷成现在，见点击会话导致列表跳动历史 bug）。
int64_t parseIsoUtcToMs(const std::string& ts)
{
    if (ts.size() != 20 || ts[4] != '-' || ts[7] != '-' || ts[10] != 'T'
        || ts[13] != ':' || ts[16] != ':' || ts[19] != 'Z')
        return -1;
    auto num = [&](size_t off, size_t n) -> int {
        int v = 0;
        for (size_t i = 0; i < n; ++i) {
            const char c = ts[off + i];
            if (c < '0' || c > '9') return -1;
            v = v * 10 + (c - '0');
        }
        return v;
    };
    const int y = num(0, 4), mo = num(5, 2), d = num(8, 2);
    const int h = num(11, 2), mi = num(14, 2), s = num(17, 2);
    if (y < 0 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60)
        return -1;
    const std::chrono::year_month_day ymd{std::chrono::year{y},
                                          std::chrono::month{static_cast<unsigned>(mo)},
                                          std::chrono::day{static_cast<unsigned>(d)}};
    if (!ymd.ok()) return -1;  // 如 2 月 30 日等非法日期
    using namespace std::chrono;
    const auto tp = sys_days{ymd} + hours{h} + minutes{mi} + seconds{s};
    return duration_cast<milliseconds>(tp.time_since_epoch()).count();
}

} // namespace

// ── 按显式 session_id 读写 ──

void SessionPersistence::save(const std::string& session_id, const llm::IMemory& memory)
{
    // 库未打开时的统一守卫：解引用空 db_ 必然崩溃（未 open 时直接安全返回）
    if (!sqlite_.isOpen()) {
        spdlog::warn("[SessionPersistence] 库未打开，跳过保存会话 {}", session_id);
        return;
    }
    sqlite_.inTransaction([&](storage::SqliteStore& s) {
        SQLite::Database& db = s.db();
        const std::string ts = storage_.nowTimestamp();
        const auto& msgs = memory.messages();

        // 1) 会话登记：upsert；已存在时仅刷新 updated_at 与空标题
        // 取舍：upsert 刻意不重置 archived 列——归档是永久封存（数据保留、列表不可见），
        // 归档后同 id 再 save() 不会复活该会话，与规格一致。
        {
            SQLite::Statement upsert(db,
                "INSERT INTO sessions (id, title, created_at, updated_at) VALUES (?, ?, ?, ?) "
                "ON CONFLICT(id) DO UPDATE SET "
                " updated_at = excluded.updated_at,"
                " title = CASE WHEN sessions.title = '' THEN excluded.title ELSE sessions.title END");
            upsert.bind(1, session_id);
            upsert.bind(2, deriveTitle(msgs));
            upsert.bind(3, ts);
            upsert.bind(4, ts);
            upsert.exec();
        }
        // 2) 快照层：全量覆盖（DELETE + 重插）
        {
            SQLite::Statement del(db, "DELETE FROM messages WHERE session_id = ?");
            del.bind(1, session_id);
            del.exec();
        }
        SQLite::Statement ins(db,
            "INSERT INTO messages (session_id, seq, role, content, tool_calls,"
            " tool_call_id, reasoning_content, preserved, is_control)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
        int seq = 1;
        for (const auto& m : msgs) {
            if (isSystem(m)) continue;
            ins.bind(1, session_id);
            bindMessageColumns(ins, m, seq++);
            ins.exec();
            ins.reset();  // step 后须 reset 方可重新 bind（SQLiteCpp 约束）
        }
    });
    spdlog::info("[SessionPersistence] 会话 {} 已保存 (快照更新)", session_id);
}

llm::Memory SessionPersistence::load(const std::string& session_id)
{
    if (!sqlite_.isOpen()) {
        spdlog::warn("[SessionPersistence] 库未打开，加载会话 {} 失败（返回空）", session_id);
        return {};
    }
    llm::Memory mem;
    sqlite_.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement stmt(s.db(),
            "SELECT seq, role, content, tool_calls, tool_call_id, reasoning_content,"
            " preserved, is_control FROM messages WHERE session_id = ? ORDER BY seq");
        stmt.bind(1, session_id);
        while (stmt.executeStep()) {
            llm::Message m = rowToMessage(stmt);
            if (m.role == llm::MessageRole::System) continue;  // 防御
            mem.inject(std::move(m));
        }
    });
    spdlog::info("[SessionPersistence] 会话 {} 已加载 ({} 条消息)", session_id, mem.size());
    return mem;
}

void SessionPersistence::appendHistory(const std::string& session_id,
                                       const std::vector<llm::Message>& messages)
{
    if (!sqlite_.isOpen()) {
        spdlog::warn("[SessionPersistence] 库未打开，跳过历史追加 {}", session_id);
        return;
    }
    std::vector<const llm::Message*> targets;
    for (const auto& m : messages) {
        if (!isSystem(m)) targets.push_back(&m);
    }
    if (targets.empty()) return;

    sqlite_.inTransaction([&](storage::SqliteStore& s) {
        SQLite::Database& db = s.db();
        // 续号：从当前最大 seq 之后连续编号
        int seq = 1;
        {
            SQLite::Statement maxq(db,
                "SELECT COALESCE(MAX(seq), 0) + 1 FROM message_history WHERE session_id = ?");
            maxq.bind(1, session_id);
            if (maxq.executeStep()) seq = maxq.getColumn(0).getInt();
        }
        SQLite::Statement ins(db,
            "INSERT INTO message_history (session_id, seq, role, content, tool_calls,"
            " tool_call_id, reasoning_content, preserved, is_control)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
        for (const llm::Message* m : targets) {
            ins.bind(1, session_id);
            bindMessageColumns(ins, *m, seq++);
            ins.exec();
            ins.reset();  // step 后须 reset 方可重新 bind（SQLiteCpp 约束）
        }
    });
    spdlog::info("[SessionPersistence] 会话 {} 完整历史追加 {} 条消息",
                 session_id, targets.size());
}

std::vector<llm::Message> SessionPersistence::loadHistory(const std::string& session_id)
{
    if (!sqlite_.isOpen()) {
        spdlog::warn("[SessionPersistence] 库未打开，读取历史 {} 失败（返回空）", session_id);
        return {};
    }
    std::vector<llm::Message> result;
    sqlite_.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement stmt(s.db(),
            "SELECT seq, role, content, tool_calls, tool_call_id, reasoning_content,"
            " preserved, is_control FROM message_history WHERE session_id = ? ORDER BY seq");
        stmt.bind(1, session_id);
        while (stmt.executeStep()) {
            llm::Message m = rowToMessage(stmt);
            if (m.role == llm::MessageRole::System) continue;
            result.push_back(std::move(m));
        }
    });
    return result;
}

// ── 会话管理 ──

std::vector<SessionInfo> SessionPersistence::listSessions()
{
    if (!sqlite_.isOpen()) {
        spdlog::warn("[SessionPersistence] 库未打开，会话列表为空");
        return {};
    }
    std::vector<SessionInfo> result;
    sqlite_.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement stmt(s.db(),
            "SELECT id, title, created_at, updated_at FROM sessions WHERE archived = 0 "
            "ORDER BY updated_at DESC");
        while (stmt.executeStep()) {
            SessionInfo info;
            info.id = stmt.getColumn(0).getString();
            info.title = stmt.getColumn(1).getString();
            info.created_at = stmt.getColumn(2).getString();
            info.updated_at = stmt.getColumn(3).getString();
            result.push_back(std::move(info));
        }
    });
    return result;
}

bool SessionPersistence::hasSession(const std::string& id)
{
    if (!sqlite_.isOpen()) {
        spdlog::warn("[SessionPersistence] 库未打开，会话存在性检查 {} 失败", id);
        return false;
    }
    return sqlite_.withLock([&](storage::SqliteStore& s) -> bool {
        SQLite::Statement q(s.db(),
            "SELECT 1 FROM sessions WHERE id = ? AND archived = 0");
        q.bind(1, id);
        return q.executeStep();
    });
}

int64_t SessionPersistence::sessionUpdatedAtMs(const std::string& id)
{
    if (!sqlite_.isOpen()) return -1;
    std::string updated_at;
    sqlite_.withLock([&](storage::SqliteStore& s) {
        SQLite::Statement q(s.db(), "SELECT updated_at FROM sessions WHERE id = ?");
        q.bind(1, id);
        if (q.executeStep()) updated_at = q.getColumn(0).getString();
    });
    if (updated_at.empty()) return -1;  // 无行（含已归档行）
    return parseIsoUtcToMs(updated_at);
}

bool SessionPersistence::sessionIdExists(const std::string& id)
{
    if (!sqlite_.isOpen()) {
        spdlog::warn("[SessionPersistence] 库未打开，会话 id 查重 {} 失败", id);
        return false;
    }
    return sqlite_.withLock([&](storage::SqliteStore& s) -> bool {
        SQLite::Statement q(s.db(), "SELECT 1 FROM sessions WHERE id = ?");
        q.bind(1, id);
        return q.executeStep();
    });
}

std::string SessionPersistence::createSession()
{
    // 库未打开时创建失败：返回空串（调用方按空串处理；SessionPool 的会话 id 自生成、
    // 不依赖本方法，无断言风险。须在锁外守卫，否则解引用空 db_ 必然崩溃）
    if (!sqlite_.isOpen()) {
        spdlog::error("[SessionPersistence] 库未打开，创建会话失败");
        return {};
    }
    const std::string id = sqlite_.inTransaction([&](storage::SqliteStore& s) -> std::string {
        const std::string ts = storage_.nowTimestamp();
        const std::string sid = makeSessionId(ts);
        SQLite::Statement ins(s.db(),
            "INSERT INTO sessions (id, title, created_at, updated_at, archived)"
            " VALUES (?, '', ?, ?, 0)");
        ins.bind(1, sid);
        ins.bind(2, ts);
        ins.bind(3, ts);
        ins.exec();
        return sid;
    });
    // 日志在事务回调外打印：先捕获返回的 id，锁内不落日志
    spdlog::info("[SessionPersistence] 新会话已创建: {}", id);
    return id;
}

bool SessionPersistence::deleteSession(const std::string& id)
{
    // 库未打开时删除失败（锁外守卫，避免解引用空 db_ 崩溃）
    if (!sqlite_.isOpen()) {
        spdlog::warn("[SessionPersistence] 库未打开，删除会话 {} 失败", id);
        return false;
    }
    const bool deleted = sqlite_.inTransaction([&](storage::SqliteStore& s) -> bool {
        SQLite::Database& db = s.db();
        // 先确认存在（未归档）再置归档，避免依赖 exec() 的变更计数返回值
        {
            SQLite::Statement q(db, "SELECT 1 FROM sessions WHERE id = ? AND archived = 0");
            q.bind(1, id);
            if (!q.executeStep()) return false;
        }
        SQLite::Statement upd(db, "UPDATE sessions SET archived = 1 WHERE id = ?");
        upd.bind(1, id);
        upd.exec();
        return true;
    });
    // 日志在事务回调外打印（仅成功归档时）
    if (deleted) spdlog::info("[SessionPersistence] 会话 {} 已删除（归档，数据保留）", id);
    return deleted;
}

std::string SessionPersistence::makeSessionId(const std::string& timestamp) const
{
    // "2026-07-27T03:15:00Z" → "s-20260727T031500Z"（沿用原格式）
    std::string compact;
    for (char c : timestamp) {
        if (c != ':' && c != '-') compact += c;
    }
    const std::string base = "s-" + compact;
    std::string candidate = base;
    int n = 2;
    // 查重含归档会话：已删除 id 不同秒复用，保持 id 全局唯一
    auto taken = [&](const std::string& id) {
        SQLite::Statement stmt(sqlite_.db(),
            "SELECT 1 FROM sessions WHERE id = ?");
        stmt.bind(1, id);
        return stmt.executeStep();
    };
    while (taken(candidate))
        candidate = base + "-" + std::to_string(n++);
    return candidate;
}

} // namespace agent