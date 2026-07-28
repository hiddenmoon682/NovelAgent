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

// 将对话序列化为 JSON 数组（会话文件与归档共用格式）。
// System 消息一律跳过：system prompt 由启动时重新组装，不落盘（见头文件说明）。
nlohmann::json serializeMessages(const llm::IMemory& memory)
{
    nlohmann::json j = nlohmann::json::array();
    for (const auto& msg : memory.messages()) {
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
        j.push_back(msg_json);
    }
    return j;
}

// 将 JSON 数组解析为 Memory（防御式：字段缺失时用默认值兜底）。
llm::Memory parseMessages(const nlohmann::json& j)
{
    llm::Memory mem;
    if (!j.is_array()) return mem;

    for (const auto& msg_json : j) {
        std::string role_str = utils::json::getOrDefault(msg_json, "role", std::string{});
        llm::Message msg;
        msg.role = llm::roleFromString(role_str);
        if (msg.role == llm::MessageRole::System) continue;  // 兼容旧格式文件中的 system 条目
        msg.content = utils::json::getOrDefault(msg_json, "content", std::string{});
        msg.reasoning_content = utils::json::getOrDefault(msg_json, "reasoning_content", std::string{});
        msg.preserved = utils::json::getOrDefault(msg_json, "preserved", false);
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
    // 同时查重 archive/：已删除会话的 id 若同秒被复用，日后归档会覆盖旧归档文件
    const std::string archive_dir = utils::file::joinPath(storage_.agentDir(), kArchiveDir);
    auto taken = [&](const std::string& id) {
        return utils::file::exists(sessionFile(id))
            || utils::file::exists(utils::file::joinPath(archive_dir, id + ".json"));
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
    if (!idx.contains("active") || !idx["active"].is_string()) return false;
    if (!idx.contains("sessions") || !idx["sessions"].is_array()) return false;

    // 每条 entry 必须含非空字符串 id，且 active 必须引用存在的会话
    bool active_found = false;
    for (const auto& e : idx["sessions"]) {
        if (!e.is_object() || !e.contains("id") || !e["id"].is_string() ||
            e["id"].get<std::string>().empty())
            return false;
        if (e["id"] == idx["active"]) active_found = true;
    }
    return active_found;
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
        // 磁盘上没有任何会话文件：按初次使用处理，新建单个空会话
        std::string ts = storage_.nowTimestamp();
        std::string id = makeSessionId(ts);
        storage_.saveJson(sessionFile(id), nlohmann::json::array());
        arr.push_back({
            {"id", id}, {"title", ""}, {"created_at", ts}, {"updated_at", ts}});
        idx["active"] = id;
    } else {
        // 优先沿用损坏索引中的 active（若仍指向存在的会话），否则取最近更新的
        std::string active = getStringField(damaged, "active", std::string{});
        bool active_ok = false;
        for (const auto& e : arr) {
            if (e["id"] == active) { active_ok = true; break; }
        }
        if (!active_ok) {
            size_t best = 0;
            for (size_t i = 1; i < arr.size(); ++i) {
                if (getStringField(arr[i], "updated_at", {}) >
                    getStringField(arr[best], "updated_at", {}))
                    best = i;
            }
            active = arr[best]["id"].get<std::string>();
        }
        idx["active"] = active;
        spdlog::info("[SessionPersistence] 索引重建完成：恢复 {} 个会话，active={}",
                     arr.size(), active);
    }
    saveIndex(idx);
    return idx;
}

void SessionPersistence::saveIndex(const nlohmann::json& idx)
{
    storage_.saveJson(utils::file::joinPath(sessionsDir(), kIndexFile), idx);
}

// ── active 会话读写 ──

void SessionPersistence::save(const llm::IMemory& memory)
{
    nlohmann::json idx = loadIndex();
    const std::string active = idx["active"].get<std::string>();

    nlohmann::json msgs = serializeMessages(memory);
    storage_.saveJson(sessionFile(active), msgs);

    for (auto& e : idx["sessions"]) {
        if (utils::json::getOrDefault(e, "id", std::string{}) != active) continue;
        e["updated_at"] = storage_.nowTimestamp();
        if (utils::json::getOrDefault(e, "title", std::string{}).empty())
            e["title"] = deriveTitle(msgs);
        break;
    }
    saveIndex(idx);
    spdlog::info("[SessionPersistence] 会话 {} 已保存 ({} 条消息)", active, msgs.size());
}

llm::Memory SessionPersistence::load()
{
    nlohmann::json idx = loadIndex();
    const std::string active = idx["active"].get<std::string>();
    llm::Memory mem = parseMessages(storage_.loadJson(sessionFile(active)));
    spdlog::info("[SessionPersistence] 会话 {} 已加载 ({} 条消息)", active, mem.size());
    return mem;
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

std::string SessionPersistence::activeSessionId()
{
    return loadIndex()["active"].get<std::string>();
}

std::string SessionPersistence::createSession()
{
    nlohmann::json idx = loadIndex();
    std::string ts = storage_.nowTimestamp();
    std::string id = makeSessionId(ts);
    storage_.saveJson(sessionFile(id), nlohmann::json::array());
    idx["sessions"].push_back({
        {"id", id}, {"title", ""}, {"created_at", ts}, {"updated_at", ts}});
    idx["active"] = id;
    saveIndex(idx);
    spdlog::info("[SessionPersistence] 新会话已创建: {}", id);
    return id;
}

bool SessionPersistence::switchSession(const std::string& id)
{
    nlohmann::json idx = loadIndex();
    bool found = false;
    for (const auto& e : idx["sessions"]) {
        if (utils::json::getOrDefault(e, "id", std::string{}) == id) { found = true; break; }
    }
    if (!found) return false;
    idx["active"] = id;
    saveIndex(idx);
    return true;
}

bool SessionPersistence::deleteSession(const std::string& id)
{
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

    // 非空内容归档后再删除会话文件（可从 archive/ 手工恢复）
    std::string path = sessionFile(id);
    nlohmann::json content = storage_.loadJson(path);
    if (content.is_array() && !content.empty()) {
        std::string archive_dir = utils::file::joinPath(storage_.agentDir(), kArchiveDir);
        utils::file::createDirs(archive_dir);
        storage_.saveJson(utils::file::joinPath(archive_dir, id + ".json"), content);
    }
    if (utils::file::exists(path))
        utils::file::removeFile(path);

    // 删除的是 active 会话 → 切到最近更新的剩余会话；一个不剩则新建空会话
    if (idx["active"] == id) {
        if (arr.empty()) {
            std::string ts = storage_.nowTimestamp();
            std::string nid = makeSessionId(ts);
            storage_.saveJson(sessionFile(nid), nlohmann::json::array());
            arr.push_back({
                {"id", nid}, {"title", ""}, {"created_at", ts}, {"updated_at", ts}});
            idx["active"] = nid;
        } else {
            size_t best = 0;
            for (size_t i = 1; i < arr.size(); ++i) {
                if (utils::json::getOrDefault(arr[i], "updated_at", std::string{}) >
                    utils::json::getOrDefault(arr[best], "updated_at", std::string{}))
                    best = i;
            }
            idx["active"] = arr[best]["id"];
        }
    }
    saveIndex(idx);
    spdlog::info("[SessionPersistence] 会话 {} 已删除（内容归档到 archive/）", id);
    return true;
}

} // namespace agent
