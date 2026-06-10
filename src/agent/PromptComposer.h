#pragma once

/// System Prompt 组装器 — Fix #5: 版本化 + hash 审计。
///
/// 每次组装 prompt 时附加版本号和内容 hash，
/// 供 ExecutionTracer 记录，实现 prompt 变更可追溯。

#include <functional>
#include <string>

namespace agent {

struct PromptComponents {
    std::string personality;
    std::string context;
    std::string task;

    bool empty() const {
        return personality.empty() && context.empty() && task.empty();
    }
};

class PromptComposer {
public:
    /// Fix #5: prompt 版本号（每次修改 prompt 模板时递增）。
    static constexpr int kPromptVersion = 2;

    /// 组装最终 system prompt（含版本标记）。
    static std::string compose(const PromptComponents& components) {
        std::string result;

        if (!components.personality.empty())
            result += components.personality;

        if (!components.context.empty()) {
            if (!result.empty()) result += "\n\n";
            result += components.context;
        }

        if (!components.task.empty()) {
            if (!result.empty()) result += "\n\n";
            result += components.task;
        }

        // Fix #5: 附加版本标记（不影响 LLM 行为，仅供审计）
        result += "\n\n[prompt_v" + std::to_string(kPromptVersion) + "]";

        return result;
    }

    /// 计算 prompt 内容的简单 hash（供 ExecutionTracer 记录）。
    static size_t hash(const std::string& prompt) {
        return std::hash<std::string>{}(prompt);
    }
};

} // namespace agent
