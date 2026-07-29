#include "llm/SSEParser.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>

namespace llm {

namespace {

// null 安全读取字符串字段：键缺失或显式 null 时回落默认值。
//
// WHY：j.value() 遇显式 null 会抛 type_error.302，部分 provider 对可选
// 字段（id/finish_reason/function.name 等）会返回显式 null；流式解析需
// 与非流式 from_json 统一容错策略。此处刻意复制 Message.h::getStringOrDefault
// 的同语义实现（而非 include 复用），避免 SSEParser 依赖 Message.h 的
// 实现细节；两处语义须保持一致：非 null 非字符串类型仍抛异常。
std::string getStringOrDefault(const nlohmann::json& j, const char* key,
                               const std::string& fallback = "") {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    return it->get<std::string>();
}

} // namespace

// ===========================================================================
// feed — 接收原始数据，按双换行切分为完整事件
// ===========================================================================

void SSEParser::feed(const std::string& data)
{
    buffer_ += data;

    size_t pos = 0;
    while (pos < buffer_.size()) {
        // 查找事件边界（空白行）：支持 \n\n 和 \r\n\r\n
        size_t endLF   = buffer_.find("\n\n", pos);
        size_t endCRLF = buffer_.find("\r\n\r\n", pos);
        size_t eventEnd = std::min(endLF, endCRLF);

        if (eventEnd == std::string::npos) {
            break; // 没有完整事件，等待更多数据
        }

        std::string eventText = buffer_.substr(pos, eventEnd - pos);
        size_t skip = (eventEnd == endCRLF) ? 4 : 2;
        pos = eventEnd + skip;

        // 去除事件文本两端的空白行
        while (!eventText.empty() && (eventText.back() == '\n' || eventText.back() == '\r')) {
            eventText.pop_back();
        }

        if (!eventText.empty()) {
            processEvent(eventText);
        }
    }

    // 保留未完成的数据
    buffer_ = buffer_.substr(pos);
}

// ===========================================================================
// processEvent — 解析单个 SSE 事件的所有 data: 行
// ===========================================================================

void SSEParser::processEvent(const std::string& eventText)
{
    std::string dataLines;
    std::istringstream stream(eventText);
    std::string line;

    while (std::getline(stream, line)) {
        // 去除行尾 \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.starts_with("data:")) {
            // SSE 规范允许 "data:" 或 "data: "（带空格）
            std::string payload = line.substr(5); // 跳过 "data:"
            if (!payload.empty() && payload[0] == ' ') {
                payload = payload.substr(1);
            }
            if (!dataLines.empty()) dataLines += '\n';
            dataLines += payload;
        }
        // 忽略 event: / id: / retry: / ":" 注释行
    }

    if (dataLines.empty()) return;

    // [DONE] 终止信号 — 发出 is_end 标记的 StreamChunk
    if (dataLines == "[DONE]") {
        if (on_chunk_) {
            StreamChunk endChunk;
            endChunk.is_end = true;
            on_chunk_(endChunk);
        }
        return;
    }

    // 解析 JSON chunk → StreamChunk
    try {
        auto j = nlohmann::json::parse(dataLines);
        processChunk(j);
    } catch (const nlohmann::json::exception& e) {
        if (on_error_) on_error_(std::string("JSON 解析失败: ") + e.what());
    }
}

// ===========================================================================
// processChunk — 从 JSON chunk 构建 StreamChunk 并发出
//
// 纯协议解析：只提取字段填充 StreamChunk，不累积、不合并。
// 跨 chunk 的状态管理由 StreamAccumulator 负责。
// ===========================================================================

void SSEParser::processChunk(const nlohmann::json& j)
{
    if (!on_chunk_) return;
    if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) {
        return;
    }

    StreamChunk chunk;

    // 顶层元数据（仅首个 chunk 携带，后续 chunk 为空字符串/0）
    // WHY：字符串字段统一走 null 安全读取，显式 null 回落空串，
    // 与 Message.h 非流式 from_json 的容错策略保持一致。
    chunk.id = getStringOrDefault(j, "id");
    chunk.model = getStringOrDefault(j, "model");
    chunk.created = j.value("created", 0);

    const auto& choice = j["choices"][0];

    // finish_reason（仅末个 chunk 携带，中间 chunk 为显式 null）
    chunk.finish_reason = getStringOrDefault(choice, "finish_reason");

    // delta — 文本和工具调用增量
    if (choice.contains("delta") && choice["delta"].is_object()) {
        const auto& delta = choice["delta"];

        // 文本增量（首个 role chunk 中 content 可为显式 null）
        chunk.content_delta = getStringOrDefault(delta, "content");

        // 思维链增量（DeepSeek thinking 模式）
        chunk.reasoning_delta = getStringOrDefault(delta, "reasoning_content");

        // 工具调用增量
        if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
            for (const auto& tc : delta["tool_calls"]) {
                ToolCallDelta tcd;
                tcd.index = tc.value("index", 0);
                tcd.id = getStringOrDefault(tc, "id");
                tcd.type = getStringOrDefault(tc, "type", "function");
                if (tc.contains("function") && tc["function"].is_object()) {
                    const auto& func = tc["function"];
                    tcd.function_name = getStringOrDefault(func, "name");
                    tcd.arguments = getStringOrDefault(func, "arguments");
                }
                chunk.tool_call_deltas.push_back(std::move(tcd));
            }
        }
    }

    // usage（仅末个 chunk 携带）
    if (j.contains("usage") && j["usage"].is_object()) {
        const auto& usage = j["usage"];
        chunk.usage.prompt_tokens = usage.value("prompt_tokens", 0);
        chunk.usage.completion_tokens = usage.value("completion_tokens", 0);
        chunk.usage.total_tokens = usage.value("total_tokens", 0);
        if (usage.contains("prompt_tokens_details") && usage["prompt_tokens_details"].is_object()) {
            chunk.usage.cached_tokens = usage["prompt_tokens_details"].value("cached_tokens", 0);
        }
        if (usage.contains("completion_tokens_details") && usage["completion_tokens_details"].is_object()) {
            chunk.usage.reasoning_tokens = usage["completion_tokens_details"].value("reasoning_tokens", 0);
        }
    }

    on_chunk_(chunk);
}

// ===========================================================================
// reset
// ===========================================================================

void SSEParser::reset()
{
    buffer_.clear();
}

} // namespace llm
