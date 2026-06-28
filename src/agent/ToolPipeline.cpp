/// ToolPipeline 实现 — Fix #1: 依赖 IToolProvider&。

#include "agent/ToolPipeline.h"
#include "agent/ParameterValidator.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace agent {

void ToolPipeline::executeAndAppend(const std::vector<llm::ToolCall>& tool_calls)
{
    for (const auto& tc : tool_calls) {
        spdlog::info("[ToolPipeline] 执行: {} (id={})", tc.function_name, tc.id);
        std::string result = executeOne(tc);
        conversation_.addToolResult(tc.id, std::move(result));
    }
}

/// 计算 UTF-8 字符串中的可见字符数（中文算 1 字，英文单词算 1 字）。
/// 仅用于显示提示信息，无需精确 token 计数。
static size_t utf8CharLen(const std::string& s) {
    size_t len = 0;
    for (size_t i = 0; i < s.size(); ++len) {
        auto c = static_cast<unsigned char>(s[i]);
        if (c <= 0x7F)      i += 1;   // ASCII
        else if (c <= 0xDF) i += 2;   // 2-byte
        else if (c <= 0xEF) i += 3;   // 3-byte (CJK)
        else                i += 4;   // 4-byte
    }
    return len;
}

std::string ToolPipeline::executeOne(const llm::ToolCall& tc)
{
    nlohmann::json args;
    if (!tc.arguments.empty()) {
        try {
            args = nlohmann::json::parse(tc.arguments);
        } catch (const nlohmann::json::parse_error& e) {
            return nlohmann::json{
                {"error", "参数 JSON 解析失败"},
                {"detail", e.what()},
                {"retryable", false},
                {"suggestion", "请检查参数格式后重试"}
            }.dump();
        }
    }

    // 参数 Schema 校验（通过 IToolProvider 获取工具定义）
    auto tool_defs = tools_.getDefinitions();
    for (const auto& def : tool_defs) {
        if (def.name == tc.function_name) {
            auto validation = ParameterValidator::validate(def.parameters, args);
            if (!validation.valid) {
                nlohmann::json err = validation.toJson();
                err["retryable"] = true;
                err["suggestion"] = "请根据 details 中的提示修正参数后重试";
                return err.dump();
            }
            break;
        }
    }

    // 通过 IToolProvider 执行（支持 RestrictedToolProvider 安全约束）
    auto result = tools_.execute(tc.function_name, args);

    // A15 修复：在 JSON 对象层面截断 content 字段，确保 LLM 拿到合法 JSON。
    // 此前在 dump() 后的原始字符串上做字节级截断，preview 几乎必然非法
    // （截在字符串值中间，引号未闭合），LLM 无法解析章节内容。
    if (result.contains("content") && result["content"].is_string()) {
        std::string& content = result["content"].get_ref<std::string&>();
        if (content.size() > kMaxContentChars) {
            const size_t orig_chars = utf8CharLen(content);
            content.resize(kMaxContentChars);
            // 回退到最后一个合法 UTF-8 字符边界
            while (!content.empty() &&
                   (static_cast<unsigned char>(content.back()) & 0xC0) == 0x80) {
                content.pop_back();
            }
            content += "\n\n[... 内容过长已截断，全长约 "
                     + std::to_string(orig_chars) + " 字。请用 read_chapter 分段读取 ...]";
        }
    }

    std::string result_str = result.dump();
    if (result_str.size() > kMaxResultChars) {
        size_t cut = kMaxResultChars;
        while (cut > kMaxResultChars / 2) {
            if (result_str[cut] == ',' || result_str[cut] == '}' || result_str[cut] == ']') break;
            --cut;
        }
        nlohmann::json truncated;
        truncated["partial"] = true;
        truncated["truncated"] = true;
        truncated["original_size"] = result_str.size();
        truncated["preview"] = result_str.substr(0, cut + 1);
        truncated["note"] = "结果过大已截断（原始 " + std::to_string(result_str.size()) + " 字符）";
        return truncated.dump();
    }
    return result_str;
}

std::string ToolPipeline::truncateResult(std::string result, size_t maxChars)
{
    if (result.size() <= maxChars) return result;
    return result.substr(0, maxChars)
         + "\n...(已截断，共 " + std::to_string(result.size()) + " 字符)";
}

} // namespace agent
