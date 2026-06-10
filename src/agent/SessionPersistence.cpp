/// SessionPersistence 实现。

#include "agent/SessionPersistence.h"

#include "llm/Conversation.h"
#include "llm/Message.h"
#include "utils/FileUtils.h"
#include "utils/JsonUtils.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>

namespace agent {

void SessionPersistence::save(const llm::Conversation& conversation)
{
    nlohmann::json j = nlohmann::json::array();
    for (const auto& msg : conversation.all()) {
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

llm::Conversation SessionPersistence::load()
{
    llm::Conversation conv;
    std::string path = utils::file::joinPath(storage_.agentDir(), kConversationFile);
    nlohmann::json j = storage_.loadJson(path);
    if (!j.is_array()) return conv;

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
        conv.add(std::move(msg));
    }
    spdlog::info("[SessionPersistence] 会话已加载 ({} 条消息)", conv.size());
    return conv;
}

void SessionPersistence::archive(const llm::Conversation& conversation)
{
    if (conversation.empty()) return;

    std::string archive_dir = utils::file::joinPath(storage_.agentDir(), kArchiveDir);
    utils::file::createDirs(archive_dir);

    // 时间戳文件名
    std::string ts = storage_.nowTimestamp();
    std::replace(ts.begin(), ts.end(), ':', '-');

    nlohmann::json j = nlohmann::json::array();
    for (const auto& msg : conversation.all()) {
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

} // namespace agent
