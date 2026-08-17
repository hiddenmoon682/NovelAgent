// SessionPersistence 实现 — SQLite 表读写（快照层 + 完整历史层）。

#include "agent/session/SessionPersistence.h"

#include "agent/context/Memory.h"
#include "llm/Message.h"
#include "storage/SqliteStore.h"

#include <SQLiteCpp/Statement.h>
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

} // namespace

// ── 按显式 session_id 读写 ──

void SessionPersistence::save(const std::string& session_id, const llm::IMemory& memory)
{
    sqlite_.inTransaction([&](storage::SqliteStore& s) {
        SQLite::Database& db = s.db();
        const std::string ts = storage_.nowTimestamp();
        const auto& msgs = memory.messages();

        // 1) 会话登记：upsert；已存在时仅刷新 updated_at 与空标题
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

std::string SessionPersistence::createSession()
{
    return sqlite_.inTransaction([&](storage::SqliteStore& s) -> std::string {
        const std::string ts = storage_.nowTimestamp();
        const std::string id = makeSessionId(ts);
        SQLite::Statement ins(s.db(),
            "INSERT INTO sessions (id, title, created_at, updated_at, archived)"
            " VALUES (?, '', ?, ?, 0)");
        ins.bind(1, id);
        ins.bind(2, ts);
        ins.bind(3, ts);
        ins.exec();
        spdlog::info("[SessionPersistence] 新会话已创建: {}", id);
        return id;
    });
}

bool SessionPersistence::deleteSession(const std::string& id)
{
    return sqlite_.inTransaction([&](storage::SqliteStore& s) -> bool {
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
        spdlog::info("[SessionPersistence] 会话 {} 已删除（归档，数据保留）", id);
        return true;
    });
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