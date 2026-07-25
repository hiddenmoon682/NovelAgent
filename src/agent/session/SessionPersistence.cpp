// SessionPersistence 实现。

#include "agent/session/SessionPersistence.h"

#include "agent/context/Memory.h"
#include "llm/Message.h"
#include "utils/FileUtils.h"
#include "utils/JsonUtils.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>

namespace agent {

void SessionPersistence::save(const llm::IMemory& memory)
{
    // 序列化为 JSON 数组：[{role, content, tool_calls?, tool_call_id?}, ...]
    // preserved 标记不持久化（由 session_meta.json 的 preserved_indices 管理）
    nlohmann::json j = nlohmann::json::array();
    for (const auto& msg : memory.all()) {
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
        j.push_back(msg_json);
    }

    std::string path = utils::file::joinPath(storage_.agentDir(), kConversationFile);
    storage_.saveJson(path, j);
    spdlog::info("[SessionPersistence] 会话已保存 ({} 条消息)", j.size());
}

llm::Memory SessionPersistence::load()
{
    llm::Memory mem;
    std::string path = utils::file::joinPath(storage_.agentDir(), kConversationFile);
    nlohmann::json j = storage_.loadJson(path);
    if (!j.is_array()) return mem;  // 文件不存在或格式异常 → 返回空对话

    // 防御式解析：每个字段独立提取，缺失时使用 getOrDefault 兜底
    for (const auto& msg_json : j) {
        std::string role_str = utils::json::getOrDefault(msg_json, "role", std::string{});
        std::string content = utils::json::getOrDefault(msg_json, "content", std::string{});
        llm::Message msg;
        msg.role = llm::roleFromString(role_str);
        msg.content = content;
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
    spdlog::info("[SessionPersistence] 会话已加载 ({} 条消息)", mem.size());
    return mem;
}

void SessionPersistence::archive(const llm::IMemory& memory)
{
    if (memory.empty()) return;

    std::string archive_dir = utils::file::joinPath(storage_.agentDir(), kArchiveDir);
    utils::file::createDirs(archive_dir);

    // 时间戳文件名
    std::string ts = storage_.nowTimestamp();
    std::replace(ts.begin(), ts.end(), ':', '-');

    nlohmann::json j = nlohmann::json::array();
    for (const auto& msg : memory.all()) {
        nlohmann::json msg_json;
        msg_json["role"] = llm::roleToString(msg.role);
        msg_json["content"] = msg.content;
        if (!msg.tool_call_id.empty()) msg_json["tool_call_id"] = msg.tool_call_id;
        j.push_back(msg_json);
    }

    std::string path = utils::file::joinPath(archive_dir, "conversation_" + ts + ".json");
    storage_.saveJson(path, j);
    spdlog::info("[SessionPersistence] 会话已归档: {}", path);
}

void SessionPersistence::saveMeta(const SessionMeta& meta)
{
    nlohmann::json j;
    j["compacted_summary"] = meta.compacted_summary;
    j["compaction_marker"] = meta.compaction_marker;
    j["token_state"]["total_input_tokens"] = meta.token_state.total_input_tokens;
    j["token_state"]["total_output_tokens"] = meta.token_state.total_output_tokens;
    j["token_state"]["request_count"] = meta.token_state.request_count;
    j["token_state"]["model_context_limit"] = meta.token_state.model_context_limit;
    j["preserved_indices"] = meta.preserved_indices;
    j["project_mtime"] = meta.project_mtime;

    std::string path = utils::file::joinPath(storage_.agentDir(), kSessionMetaFile);
    storage_.saveJson(path, j);
    spdlog::info("[SessionPersistence] 会话元数据已保存 ({} preserved, compact={})",
                 meta.preserved_indices.size(), !meta.compacted_summary.empty());
}

SessionMeta SessionPersistence::loadMeta() const
{
    SessionMeta meta;
    std::string path = utils::file::joinPath(storage_.agentDir(), kSessionMetaFile);
    nlohmann::json j;
    try {
        j = storage_.loadJson(path);
    } catch (...) {
        return meta;  // 文件不存在或损坏，返回默认值
    }

    meta.compacted_summary = utils::json::getOrDefault(j, "compacted_summary", std::string{});
    meta.compaction_marker = utils::json::getOrDefault(j, "compaction_marker", 0);
    if (j.contains("token_state")) {
        meta.token_state.total_input_tokens = utils::json::getOrDefault(j["token_state"], "total_input_tokens", 0);
        meta.token_state.total_output_tokens = utils::json::getOrDefault(j["token_state"], "total_output_tokens", 0);
        meta.token_state.request_count = utils::json::getOrDefault(j["token_state"], "request_count", 0);
        meta.token_state.model_context_limit = utils::json::getOrDefault(j["token_state"], "model_context_limit", 131072);
    }
    if (j.contains("preserved_indices") && j["preserved_indices"].is_array()) {
        meta.preserved_indices = j["preserved_indices"].get<std::vector<size_t>>();
    }
    meta.project_mtime = utils::json::getOrDefault(j, "project_mtime", static_cast<int64_t>(0));

    spdlog::info("[SessionPersistence] 会话元数据已加载 ({} preserved, compact={} chars, requests={})",
                 meta.preserved_indices.size(), meta.compacted_summary.size(), meta.token_state.request_count);
    return meta;
}

} // namespace agent
