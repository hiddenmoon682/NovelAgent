// ToolPipeline 实现 — 并发执行：只读工具通过 ThreadPool 并发，写工具串行。

#include "agent/ToolPipeline.h"
#include "agent/ParameterValidator.h"
#include "agent/ThreadPool.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <future>
#include <string_view>

namespace agent {

bool ToolPipeline::isReadOnly(const std::string& name) {
    static constexpr std::array kPrefixes = {
        std::string_view{"get_"},
        std::string_view{"list_"},
        std::string_view{"read_"},
        std::string_view{"search_"},
    };
    for (auto prefix : kPrefixes) {
        if (name.starts_with(prefix))
            return true;
    }
    return false;
}

llm::ConversationDiff ToolPipeline::execute(const std::vector<llm::ToolCall>& tool_calls)
{
    llm::ConversationDiff diff;

    static constexpr std::array kSettingTools = {
        std::string_view{"create_character"}, std::string_view{"update_character"},
        std::string_view{"create_setting"},   std::string_view{"update_setting"},
        std::string_view{"create_world_rule"},std::string_view{"update_world_rule"},
        std::string_view{"add_character_development"}
    };

    const size_t n = tool_calls.size();

    // 无 ThreadPool 或只有一个工具调用时走串行路径
    if (!pool_ || n <= 1) {
        for (const auto& tc : tool_calls) {
            spdlog::info("[ToolPipeline] 执行: {} (id={})", tc.function_name, tc.id);
            std::string result = executeOne(tc);
            size_t idx = diff.added.size();
            diff.added.push_back(llm::Message::toolResult(tc.id, std::move(result)));
            if (std::ranges::find(kSettingTools, tc.function_name) != kSettingTools.end())
                diff.pinned_indices.push_back(idx);
        }
        return diff;
    }

    // 并发路径：只读工具提交到 ThreadPool，写工具在主线程串行
    // results[i] 对应 tool_calls[i] 的执行结果
    std::vector<std::string> results(n);
    std::vector<std::future<std::string>> futures(n);
    std::vector<bool> is_async(n, false);

    // 确保 schema 缓存已填充（并发前完成，避免竞态）
    if (!cache_populated_) {
        for (const auto& def : tools_.getDefinitions())
            schema_cache_[def.name] = def.parameters;
        cache_populated_ = true;
    }

    for (size_t i = 0; i < n; ++i) {
        const auto& tc = tool_calls[i];
        if (isReadOnly(tc.function_name)) {
            // 只读工具：异步执行
            is_async[i] = true;
            futures[i] = pool_->submit([this, &tc]() -> std::string {
                return executeOne(tc);
            });
        }
    }

    // 写工具在主线程按序执行（保证 Project 修改的顺序性）
    for (size_t i = 0; i < n; ++i) {
        if (is_async[i])
            continue;
        const auto& tc = tool_calls[i];
        spdlog::info("[ToolPipeline] 串行执行: {} (id={})", tc.function_name, tc.id);
        results[i] = executeOne(tc);
    }

    // 收集异步结果
    for (size_t i = 0; i < n; ++i) {
        if (is_async[i]) {
            spdlog::info("[ToolPipeline] 并发完成: {} (id={})",
                         tool_calls[i].function_name, tool_calls[i].id);
            results[i] = futures[i].get();
        }
    }

    // 按原序组装 diff
    for (size_t i = 0; i < n; ++i) {
        size_t idx = diff.added.size();
        diff.added.push_back(llm::Message::toolResult(tool_calls[i].id, std::move(results[i])));
        if (std::ranges::find(kSettingTools, tool_calls[i].function_name) != kSettingTools.end())
            diff.pinned_indices.push_back(idx);
    }

    return diff;
}

// 计算 UTF-8 字符串中的可见字符数（中文算 1 字，英文单词算 1 字）。
// 仅用于显示提示信息，无需精确 token 计数。
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

// 截断 UTF-8 字符串至多 max_chars 个字符，返回安全的字节截断位置。
// 与 utf8CharLen 使用相同的迭代逻辑，确保截断位置落在合法字符边界。
static size_t utf8CharTruncatePos(const std::string& s, size_t max_chars) {
    size_t byte_pos = 0;
    size_t char_count = 0;
    while (char_count < max_chars && byte_pos < s.size()) {
        auto c = static_cast<unsigned char>(s[byte_pos]);
        if (c <= 0x7F)      byte_pos += 1;
        else if (c <= 0xDF) byte_pos += 2;
        else if (c <= 0xEF) byte_pos += 3;
        else                byte_pos += 4;
        ++char_count;
    }
    return byte_pos;
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

    // 参数 Schema 校验（C9: 用按名缓存替代每次全量 getDefinitions 拷贝 + 线性查找）
    // 首次调用时构建 name→parameters schema 的缓存，后续 O(1) 查找。
    if (!cache_populated_) {
        for (const auto& def : tools_.getDefinitions()) {
            schema_cache_[def.name] = def.parameters;
        }
        cache_populated_ = true;
    }
    auto cache_it = schema_cache_.find(tc.function_name);
    if (cache_it != schema_cache_.end()) {
        auto validation = ParameterValidator::validate(cache_it->second, args);
        if (!validation.valid) {
            nlohmann::json err = validation.toJson();
            err["retryable"] = true;
            err["suggestion"] = "请根据 details 中的提示修正参数后重试";
            return err.dump();
        }
    }

    // 通过 IToolProvider 执行（支持 RestrictedToolProvider 安全约束）
    auto result = tools_.execute(tc.function_name, args);

    // A15 修复：在 JSON 对象层面截断 content 字段，确保 LLM 拿到合法 JSON。
    // 此前在 dump() 后的原始字符串上做字节级截断，preview 几乎必然非法
    // （截在字符串值中间，引号未闭合），LLM 无法解析章节内容。
    // #7 修复：使用字符感知截断替换字节 resize，确保 CJK 文本不会被过度截断。
    if (result.contains("content") && result["content"].is_string()) {
        std::string& content = result["content"].get_ref<std::string&>();
        if (utf8CharLen(content) > kMaxContentChars) {
            const size_t orig_chars = utf8CharLen(content);
            const size_t byte_pos = utf8CharTruncatePos(content, kMaxContentChars);
            content.resize(byte_pos);
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

} // namespace agent
