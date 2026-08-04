// SessionPersistence 实现 — 多会话索引 + 会话文件读写。

#include "agent/session/SessionPersistence.h"

#include "agent/context/Memory.h"
#include "llm/Message.h"
#include "utils/FileUtils.h"
#include "utils/JsonUtils.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>

namespace agent {

namespace {

// 将单条消息序列化为 JSON 对象（共用于上下文快照与完整历史层）。
nlohmann::json serializeMessage(const llm::Message& msg)
{
    nlohmann::json msg_json;
    msg_json["role"] = llm::roleToString(msg.role);
    msg_json["content"] = msg.content;
    if (!msg.tool_calls.empty()) {
        nlohmann::json tcs = nlohmann::json::array();
        for (const auto& tc : msg.tool_calls) {
            nlohmann::json tc_json;
            tc_json["id"] = tc.id;
            tc_json["type"] = tc.type;
            tc_json["function"] = {{"name", tc.function_name}, {"arguments", tc.arguments}};
            tcs.push_back(tc_json);
        }
        msg_json["tool_calls"] = tcs;
    }
    if (!msg.tool_call_id.empty()) msg_json["tool_call_id"] = msg.tool_call_id;
    if (!msg.reasoning_content.empty()) msg_json["reasoning_content"] = msg.reasoning_content;
    if (msg.preserved) msg_json["preserved"] = true;
    if (msg.is_control) msg_json["is_control"] = true;  // P6：控制消息标记随会话持久化
    return msg_json;
}

// 将对话序列化为 JSON 数组（会话文件与归档共用格式）。
// System 消息一律跳过：system prompt 由启动时重新组装，不落盘（见头文件说明）。
nlohmann::json serializeMessages(const llm::IMemory& memory)
{
    nlohmann::json j = nlohmann::json::array();
    for (const auto& msg : memory.messages()) {
        j.push_back(serializeMessage(msg));
    }
    return j;
}

// 将单条消息 JSON 对象解析为 Message（防御式：字段缺失时用默认值兜底）。
// System 角色返回空消息（由调用方跳过），与 parseMessages 的语义一致。
llm::Message parseMessage(const nlohmann::json& msg_json)
{
    std::string role_str = utils::json::getOrDefault(msg_json, "role", std::string{});
    llm::Message msg;
    msg.role = llm::roleFromString(role_str);
    msg.content = utils::json::getOrDefault(msg_json, "content", std::string{});
    msg.reasoning_content = utils::json::getOrDefault(msg_json, "reasoning_content", std::string{});
    msg.preserved = utils::json::getOrDefault(msg_json, "preserved", false);
    msg.is_control = utils::json::getOrDefault(msg_json, "is_control", false);
    if (msg_json.contains("tool_call_id"))
        msg.tool_call_id = msg_json["tool_call_id"].get<std::string>();
    if (msg_json.contains("tool_calls") && msg_json["tool_calls"].is_array()) {
        for (const auto& tc_json : msg_json["tool_calls"]) {
            llm::ToolCall tc;
            tc.id = utils::json::getOrDefault(tc_json, "id", std::string{});
            tc.type = utils::json::getOrDefault(tc_json, "type", std::string{});
            if (tc_json.contains("function")) {
                tc.function_name = utils::json::getOrDefault(tc_json["function"], "name", std::string{});
                tc.arguments = utils::json::getOrDefault(tc_json["function"], "arguments", std::string{});
            }
            msg.tool_calls.push_back(tc);
        }
    }
    return msg;
}

// 将 JSON 数组解析为 Memory（防御式：字段缺失时用默认值兜底）。
llm::Memory parseMessages(const nlohmann::json& j)
{
    llm::Memory mem;
    if (!j.is_array()) return mem;

    for (const auto& msg_json : j) {
        llm::Message msg = parseMessage(msg_json);
        if (msg.role == llm::MessageRole::System) continue;  // 兼容旧格式文件中的 system 条目
        mem.inject(std::move(msg));
    }
    return mem;
}

// UTF-8 安全截断：最多保留 max_bytes 字节，退到字符边界，截断时追加省略号。
std::string utf8Truncate(const std::string& s, size_t max_bytes)
{
    if (s.size() <= max_bytes) return s;
    size_t end = max_bytes;
    // 0b10xxxxxx 为多字节字符的续字节，回退到字符起始位置
    while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) --end;
    return s.substr(0, end) + "…";
}

// 从消息数组提取首条 user 消息的首行作为会话标题；无 user 消息返回空。
std::string deriveTitle(const nlohmann::json& messages)
{
    for (const auto& m : messages) {
        if (utils::json::getOrDefault(m, "role", std::string{}) != "user") continue;
        std::string content = utils::json::getOrDefault(m, "content", std::string{});
        if (auto nl = content.find('\n'); nl != std::string::npos)
            content = content.substr(0, nl);
        return utf8Truncate(content, 30);
    }
    return {};
}

// 从会话 id 反推创建时间："s-20260727T031500Z[-n]" → "2026-07-27T03:15:00Z"；
// 格式不符时返回空（重建索引时的时间戳兜底）。
std::string timestampFromId(const std::string& id)
{
    if (id.size() < 18 || id.compare(0, 2, "s-") != 0) return {};
    const std::string c = id.substr(2, 16);  // 期望形如 20260727T031500Z
    if (c.size() != 16 || c[8] != 'T' || c[15] != 'Z') return {};
    for (int i : {0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14}) {
        if (c[i] < '0' || c[i] > '9') return {};
    }
    return c.substr(0, 4) + "-" + c.substr(4, 2) + "-" + c.substr(6, 2) + "T" +
           c.substr(9, 2) + ":" + c.substr(11, 2) + ":" + c.substr(13, 2) + "Z";
}

// 安全读取字符串字段：类型不符（如 title 是数字）时返回默认值而非抛异常。
std::string getStringField(const nlohmann::json& j, const char* key,
                            const std::string& fallback)
{
    if (j.is_object() && j.contains(key) && j[key].is_string())
        return j[key].get<std::string>();
    return fallback;
}

} // namespace

// ── 路径与索引 ──

std::string SessionPersistence::sessionsDir() const
{
    return utils::file::joinPath(storage_.agentDir(), kSessionsDir);
}

std::string SessionPersistence::sessionFile(const std::string& id) const
{
    return utils::file::joinPath(sessionsDir(), id + ".json");
}

std::string SessionPersistence::historyFile(const std::string& id) const
{
    return utils::file::joinPath(sessionsDir(), id + ".history");
}

std::string SessionPersistence::makeSessionId(const std::string& timestamp) const
{
    // "2026-07-27T03:15:00Z" → "s-20260727T031500Z"（文件名安全）
    std::string compact;
    for (char c : timestamp) {
        if (c != ':' && c != '-') compact += c;
    }
    std::string base = "s-" + compact;
    std::string candidate = base;
    int n = 2;
    // 同时查重 archive/ 与完整历史层：已删除会话的 id 若同秒被复用，日后归档
    // 会覆盖旧归档文件；残留的 <id>.history 也会被误追加。
    const std::string archive_dir = utils::file::joinPath(storage_.agentDir(), kArchiveDir);
    auto taken = [&](const std::string& id) {
        return utils::file::exists(sessionFile(id))
            || utils::file::exists(historyFile(id))
            || utils::file::exists(utils::file::joinPath(archive_dir, id + ".json"))
            || utils::file::exists(utils::file::joinPath(archive_dir, id + ".history"));
    };
    while (taken(candidate))
        candidate = base + "-" + std::to_string(n++);
    return candidate;
}

nlohmann::json SessionPersistence::loadIndex()
{
    std::string index_path = utils::file::joinPath(sessionsDir(), kIndexFile);
    nlohmann::json idx = storage_.loadJson(index_path);
    if (indexValid(idx)) return idx;

    // 索引缺失（初次使用）或损坏：扫描目录重建，绝不直接重置——
    // 直接重置会将磁盘上既有的 sessions/<id>.json 全部孤儿化（静默丢数据）。
    if (utils::file::exists(index_path)) {
        spdlog::warn("[SessionPersistence] index.json 无效或损坏，扫描目录重建索引");
    }
    return rebuildIndexFromDisk(idx);
}

bool SessionPersistence::indexValid(const nlohmann::json& idx) const
{
    if (!idx.is_object()) return false;
    if (!idx.contains("sessions") || !idx["sessions"].is_array()) return false;

    // 每条 entry 必须含非空字符串 id（D3：无 active 字段）
    for (const auto& e : idx["sessions"]) {
        if (!e.is_object() || !e.contains("id") || !e["id"].is_string() ||
            e["id"].get<std::string>().empty())
            return false;
    }
    return true;
}

nlohmann::json SessionPersistence::rebuildIndexFromDisk(const nlohmann::json& damaged)
{
    utils::file::createDirs(sessionsDir());

    // 从损坏索引中回收指定 id 的元数据（title/created_at/updated_at）
    auto metaFor = [&damaged](const std::string& id) -> nlohmann::json {
        if (damaged.is_object() && damaged.contains("sessions") &&
            damaged["sessions"].is_array()) {
            for (const auto& e : damaged["sessions"]) {
                if (e.is_object() && e.contains("id") && e["id"].is_string() &&
                    e["id"].get<std::string>() == id)
                    return e;
            }
        }
        return nlohmann::json::object();
    };

    nlohmann::json idx = nlohmann::json::object();
    idx["sessions"] = nlohmann::json::array();

    for (const auto& name : utils::file::listDir(sessionsDir())) {
        if (name == kIndexFile) continue;
        if (name.size() <= 5 || name.compare(name.size() - 5, 5, ".json") != 0) continue;
        const std::string id = name.substr(0, name.size() - 5);
        nlohmann::json content = storage_.loadJson(sessionFile(id));
        if (!content.is_array()) continue;  // 非会话文件，跳过

        nlohmann::json meta = metaFor(id);
        std::string fallback_ts = timestampFromId(id);
        if (fallback_ts.empty()) fallback_ts = storage_.nowTimestamp();
        idx["sessions"].push_back({
            {"id", id},
            {"title", getStringField(meta, "title", deriveTitle(content))},
            {"created_at", getStringField(meta, "created_at", fallback_ts)},
            {"updated_at", getStringField(meta, "updated_at", fallback_ts)}});
    }

    auto& arr = idx["sessions"];
    if (arr.empty()) {
        // 磁盘上没有任何会话文件：按初次使用处理，返回空索引（D3：不自动创建幽灵会话）
        spdlog::info("[SessionPersistence] 索引重建完成：磁盘无会话文件");
    } else {
        spdlog::info("[SessionPersistence] 索引重建完成：恢复 {} 个会话", arr.size());
    }
    saveIndex(idx);
    return idx;
}

void SessionPersistence::saveIndex(const nlohmann::json& idx)
{
    storage_.saveJson(utils::file::joinPath(sessionsDir(), kIndexFile), idx);
}

// ── 按显式 session_id 保存/加载（D3：不依赖 active）──

void SessionPersistence::save(const std::string& session_id, const llm::IMemory& memory)
{
    std::lock_guard<std::mutex> lock(index_mutex_);
    nlohmann::json idx = loadIndex();

    nlohmann::json msgs = serializeMessages(memory);
    storage_.saveJson(sessionFile(session_id), msgs);

    // 会话已登记则更新 updated_at/标题；否则登记新会话（多会话按 id 隔离，不设 active）
    bool found = false;
    for (auto& e : idx["sessions"]) {
        if (utils::json::getOrDefault(e, "id", std::string{}) != session_id) continue;
        e["updated_at"] = storage_.nowTimestamp();
        if (utils::json::getOrDefault(e, "title", std::string{}).empty())
            e["title"] = deriveTitle(msgs);
        found = true;
        break;
    }
    if (!found) {
        const std::string ts = storage_.nowTimestamp();
        idx["sessions"].push_back({
            {"id", session_id}, {"title", deriveTitle(msgs)},
            {"created_at", ts}, {"updated_at", ts}});
    }
    saveIndex(idx);
    spdlog::info("[SessionPersistence] 会话 {} 已保存 ({} 条消息)", session_id, msgs.size());
}

llm::Memory SessionPersistence::load(const std::string& session_id)
{
    llm::Memory mem = parseMessages(storage_.loadJson(sessionFile(session_id)));
    spdlog::info("[SessionPersistence] 会话 {} 已加载 ({} 条消息)", session_id, mem.size());
    return mem;
}

// 追加被压缩消息到指定会话的完整历史层（append-only，原子写）。
// 采用 JSONL 格式：每行一条消息，逐次追加到文件末尾，永不覆盖历史。
//
// 线程约束（重要）：read-modify-write 在并发调用下存在 lost-update 风险——
// 两个并发批次各自读到旧文件后重写，后写者会覆盖先写者的批次。当前应用
// 假设持久化路径单线程：Agent 状态机串行化 process/compactConversation，
// 同一会话的 appendHistory 不会并发。writeText 的原子写仅保证崩溃时文件
// 完整（要么旧内容要么新内容），不提供并发安全。调用方须维持该单线程约束。
void SessionPersistence::appendHistory(const std::string& session_id,
                                        const std::vector<llm::Message>& messages)
{
    if (messages.empty()) return;

    // 追加重写：先读现有内容，再在末尾追加新行，整文件原子写回。
    // 历史层只读多写少（每次压缩追加一批），单线程下重写可行。
    std::string path = historyFile(session_id);
    std::string existing = utils::file::readText(path);
    std::string appended;
    for (const auto& msg : messages) {
        appended += serializeMessage(msg).dump() + "\n";
    }
    utils::file::writeText(path, existing + appended);
    spdlog::info("[SessionPersistence] 会话 {} 完整历史追加 {} 条消息",
                 session_id, messages.size());
}

// 读取指定会话的完整历史层：按 JSONL 逐行解析为消息，按时间顺序返回。
std::vector<llm::Message> SessionPersistence::loadHistory(const std::string& session_id)
{
    std::vector<llm::Message> result;
    std::string path = historyFile(session_id);
    if (!utils::file::exists(path)) return result;

    std::string text = utils::file::readText(path);
    // 按行切分（JSONL：每行一条消息，appendHistory 追加时以 \n 分隔）
    std::string line;
    std::istringstream iss(text);
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        try {
            auto j = nlohmann::json::parse(line);
            if (j.is_object()) {
                llm::Message msg = parseMessage(j);
                if (msg.role != llm::MessageRole::System) result.push_back(std::move(msg));
            }
        } catch (const std::exception& e) {
            // 单行损坏不丢弃其余历史，记日志后跳过
            spdlog::warn("[SessionPersistence] 会话 {} 历史行解析失败，跳过: {}",
                         session_id, e.what());
        }
    }
    return result;
}

// ── 会话管理 ──

std::vector<SessionInfo> SessionPersistence::listSessions()
{
    nlohmann::json idx = loadIndex();
    std::vector<SessionInfo> result;
    for (const auto& e : idx["sessions"]) {
        SessionInfo info;
        info.id = utils::json::getOrDefault(e, "id", std::string{});
        info.title = utils::json::getOrDefault(e, "title", std::string{});
        info.created_at = utils::json::getOrDefault(e, "created_at", std::string{});
        info.updated_at = utils::json::getOrDefault(e, "updated_at", std::string{});
        result.push_back(std::move(info));
    }
    // ISO 8601 UTC 字符串可直接按字典序比较
    std::sort(result.begin(), result.end(),
              [](const SessionInfo& a, const SessionInfo& b) {
                  return a.updated_at > b.updated_at;
              });
    return result;
}

std::string SessionPersistence::createSession()
{
    std::lock_guard<std::mutex> lock(index_mutex_);
    nlohmann::json idx = loadIndex();
    std::string ts = storage_.nowTimestamp();
    std::string id = makeSessionId(ts);
    storage_.saveJson(sessionFile(id), nlohmann::json::array());
    idx["sessions"].push_back({
        {"id", id}, {"title", ""}, {"created_at", ts}, {"updated_at", ts}});
    saveIndex(idx);
    spdlog::info("[SessionPersistence] 新会话已创建: {}", id);
    return id;
}

bool SessionPersistence::deleteSession(const std::string& id)
{
    std::lock_guard<std::mutex> lock(index_mutex_);
    nlohmann::json idx = loadIndex();
    auto& arr = idx["sessions"];
    bool found = false;
    for (size_t i = 0; i < arr.size(); ++i) {
        if (utils::json::getOrDefault(arr[i], "id", std::string{}) == id) {
            arr.erase(i);
            found = true;
            break;
        }
    }
    if (!found) return false;

    // 归档后再删除会话文件：上下文快照（<id>.json）与完整历史（<id>.history）
    // 一并归档到 archive/，保证删除会话后完整历史同样可恢复（双层持久化语义与
    // 快照层一致，而非把历史层孤儿化）。
    const std::string archive_dir = utils::file::joinPath(storage_.agentDir(), kArchiveDir);
    utils::file::createDirs(archive_dir);

    std::string path = sessionFile(id);
    nlohmann::json content = storage_.loadJson(path);
    if (content.is_array() && !content.empty()) {
        storage_.saveJson(utils::file::joinPath(archive_dir, id + ".json"), content);
    }
    if (utils::file::exists(path))
        utils::file::removeFile(path);

    // 完整历史层：非空则复制到 archive/<id>.history 后删除原文件（原子写，可从
    // archive/ 手工恢复，与快照归档同一约定）。
    std::string history_path = historyFile(id);
    if (utils::file::exists(history_path)) {
        utils::file::writeText(utils::file::joinPath(archive_dir, id + ".history"),
                               utils::file::readText(history_path));
        utils::file::removeFile(history_path);
    }

    saveIndex(idx);
    spdlog::info("[SessionPersistence] 会话 {} 已删除（快照与完整历史归档到 archive/）", id);
    return true;
}

} // namespace agent
