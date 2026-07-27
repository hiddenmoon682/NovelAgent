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

namespace {

// 将对话序列化为 JSON 数组（save/archive 共用，保证归档与主文件保真度一致）。
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

} // namespace

void SessionPersistence::save(const llm::IMemory& memory)
{
    nlohmann::json j = serializeMessages(memory);
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
    spdlog::info("[SessionPersistence] 会话已加载 ({} 条消息)", mem.size());
    return mem;
}

void SessionPersistence::archive(const llm::IMemory& memory)
{
    if (memory.messages().empty()) return;

    std::string archive_dir = utils::file::joinPath(storage_.agentDir(), kArchiveDir);
    utils::file::createDirs(archive_dir);

    // 时间戳文件名
    std::string ts = storage_.nowTimestamp();
    std::replace(ts.begin(), ts.end(), ':', '-');

    std::string path = utils::file::joinPath(archive_dir, "conversation_" + ts + ".json");
    storage_.saveJson(path, serializeMessages(memory));
    spdlog::info("[SessionPersistence] 会话已归档: {}", path);
}

} // namespace agent
