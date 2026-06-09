#pragma once

/// System Prompt 显式组装器 — 消除人格提示词与上下文提示词的隐式拼接。
///
/// 原来: system_prompt_ + "\n\n" + assembly.system_prompt (散落在 Agent 中)
/// 现在: PromptComposer::compose({personality, context, task})
///
/// 每种提示词组件有明确的语义标签，新增组件（如 Phase 4 的摘要）只需加字段。

#include <string>

namespace agent {

struct PromptComponents {
    std::string personality;   // AI 人格提示词 ("你是一个网文写作助手")
    std::string context;       // 项目/章节上下文 (ContextManager 产出)
    std::string task;          // 当前任务描述 (可选，如 "写第三章")

    bool empty() const {
        return personality.empty() && context.empty() && task.empty();
    }
};

class PromptComposer {
public:
    /// 将多个提示词组件组装为最终 system prompt。
    /// 空组件自动跳过，不会产生多余的空行。
    static std::string compose(const PromptComponents& components) {
        std::string result;

        if (!components.personality.empty()) {
            result += components.personality;
        }

        if (!components.context.empty()) {
            if (!result.empty()) result += "\n\n";
            result += components.context;
        }

        if (!components.task.empty()) {
            if (!result.empty()) result += "\n\n";
            result += components.task;
        }

        return result;
    }
};

} // namespace agent
