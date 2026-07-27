// SessionPersistence 实现 — 多会话索引 + 会话文件读写 + 旧格式迁移。

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
    while (utils::file::exists(sessionFile(candidate)))
        candidate = base + "-" + std::to_string(n++);
    return candidate;
}

nlohmann::json SessionPersistence::loadIndex()
{
    std::string index_path = utils::file::joinPath(sessionsDir(), kIndexFile);
    nlohmann::json idx = storage_.loadJson(index_path);
    if (idx.is_object() && idx.contains("active") &&
        idx.contains("sessions") && idx["sessions"].is_array()) {
        return idx;
    }

    // 初次使用：建立索引，旧单会话格式自动迁移为首个会话
    utils::file::createDirs(sessionsDir());
    idx = nlohmann::json::object();
    idx["sessions"] = nlohmann::json::array();

    std::string ts = storage_.nowTimestamp();
    std::string id = makeSessionId(ts);
    nlohmann::json entry = {
        {"id", id}, {"title", ""}, {"created_at", ts}, {"updated_at", ts}};

    std::string legacy_path =
        utils::file::joinPath(storage_.agentDir(), kLegacyConversationFile);
    nlohmann::json legacy = storage_.loadJson(legacy_path);
    if (legacy.is_array() && !legacy.empty()) {
        storage_.saveJson(sessionFile(id), legacy);
        entry["title"] = deriveTitle(legacy);
        spdlog::info("[SessionPersistence] 旧版 conversation.json 已迁移为会话 {} ({} 条消息)",
                     id, legacy.size());
    } else {
        storage_.saveJson(sessionFile(id), nlohmann::json::array());
    }
    if (utils::file::exists(legacy_path))
        utils::file::removeFile(legacy_path);

    idx["sessions"].push_back(entry);
    idx["active"] = id;
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
