#include "llm/SSEParser.h"

#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>

namespace llm {

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

    // [DONE] 终止信号（非 JSON）
    if (dataLines == "[DONE]") {
        if (on_done_) on_done_();
        return;
    }

    // 解析 JSON chunk
    try {
        auto j = nlohmann::json::parse(dataLines);
        processChunk(j);
    } catch (const nlohmann::json::exception& e) {
        if (on_error_) on_error_(std::string("JSON 解析失败: ") + e.what());
    }
}

// ===========================================================================
// processChunk — 从单个 chunk JSON 中提取 content / tool_calls
// ===========================================================================

void SSEParser::processChunk(const nlohmann::json& j)
{
    if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) {
        return;
    }

    const auto& choice = j["choices"][0];
    if (!choice.contains("delta") || !choice["delta"].is_object()) {
        return;
    }

    const auto& delta = choice["delta"];

    // 文本增量
    if (delta.contains("content") && delta["content"].is_string()) {
        std::string token = delta["content"].get<std::string>();
        if (!token.empty() && on_token_) {
            on_token_(token);
        }
    }

    // 工具调用增量
    if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
        for (const auto& tc : delta["tool_calls"]) {
            ToolCall call;
            if (tc.contains("id") && tc["id"].is_string()) {
                call.id = tc["id"].get<std::string>();
            }
            if (tc.contains("type") && tc["type"].is_string()) {
                call.type = tc["type"].get<std::string>();
            }
            if (tc.contains("function") && tc["function"].is_object()) {
                const auto& func = tc["function"];
                if (func.contains("name") && func["name"].is_string()) {
                    call.function_name = func["name"].get<std::string>();
                }
                if (func.contains("arguments") && func["arguments"].is_string()) {
                    call.arguments = func["arguments"].get<std::string>();
                }
            }
            if (on_tool_call_) on_tool_call_(call);
        }
    }
}

// ===========================================================================
// reset
// ===========================================================================

void SSEParser::reset()
{
    buffer_.clear();
}

} // namespace llm
