#pragma once

// SSE 测试数据构造辅助 — 将 test_llm_client 中手工拼接 JSON 字符串的逻辑
// 抽取为可复用的辅助函数，让测试代码更简洁、可读。
//
// 这些函数仅供测试使用，不应出现在生产代码中。

#include <nlohmann/json.hpp>
#include <string>

namespace llm::test {

// 构造文本增量 SSE chunk（OpenAI 流式 delta 格式）。
// content  文本增量内容
// id       响应 ID（首个 chunk 携带）
// model    模型名
inline std::string sseContentChunk(const std::string& content,
                                   const std::string& id = "test-id",
                                   const std::string& model = "test") {
    using json = nlohmann::json;
    json j;
    j["id"] = id;
    j["model"] = model;
    j["created"] = 1234;
    j["choices"] = json::array({{
        {"index", 0},
        {"delta", {{"content", content}}}
    }});
    return "data: " + j.dump() + "\n\n";
}

// 构造流结束 SSE chunk（携带 finish_reason + usage）。
// reason  结束原因，如 "stop" / "length" / "tool_calls"
inline std::string sseFinishChunk(const std::string& reason = "stop") {
    using json = nlohmann::json;
    json j;
    j["choices"] = json::array({{
        {"index", 0},
        {"delta", json::object()},
        {"finish_reason", reason}
    }});
    j["usage"] = {
        {"prompt_tokens", 10},
        {"completion_tokens", 5},
        {"total_tokens", 15}
    };
    return "data: " + j.dump() + "\n\n";
}

// SSE 流终止信号
constexpr const char* sseDone = "data: [DONE]\n\n";

// 构造 tool_call 增量 SSE chunk。
//
// 每个 chunk 可携带一个 tool_call 的增量字段：
// - chunk 0（首次）：携带 index + id + type + function.name，arguments 为空
// - chunk 1+：仅携带 index + arguments 增量片段
//
// index       tool_call 在数组中的索引
// call_id     工具调用唯一 ID
// func_name   函数名
// arguments   JSON 参数的增量片段
inline std::string sseToolCallChunk(int index,
                                    const std::string& call_id,
                                    const std::string& func_name,
                                    const std::string& arguments = "") {
    using json = nlohmann::json;
    json tc;
    tc["index"] = index;

    // 首个 chunk：携带 id、type、function name
    if (!call_id.empty()) {
        tc["id"] = call_id;
        tc["type"] = "function";
        tc["function"] = {{"name", func_name}, {"arguments", arguments}};
    } else {
        // 后续增量 chunk：仅携带 arguments 片段
        tc["function"] = {{"arguments", arguments}};
    }

    json j;
    j["choices"] = json::array({{
        {"index", 0},
        {"delta", {{"tool_calls", json::array({tc})}}}
    }});
    return "data: " + j.dump() + "\n\n";
}

} // namespace llm::test
