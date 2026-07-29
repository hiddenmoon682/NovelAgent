#pragma once

#include "agent/tool/IToolProvider.h"
#include "llm/Message.h"

#include <set>
#include <string>
#include <vector>

namespace agent {

// 渐进式工具上下文提供者 — 根据对话上下文动态选择发送给 LLM 的工具定义子集。
//
// 核心思路：初始只暴露核心工具（读写章节、查看大纲），
// 随用户输入中出现的关键词逐步激活对应类别的工具，
// 减少每次请求的 token 消耗并降低 LLM 的选择负担。
//
// 一旦某个类别被激活，在当前会话内保持激活（只增不减）。
class ContextualToolProvider {
public:
    // 构造时注入底层工具提供者。
    // @param tools 完整工具集的提供者；非拥有引用，
    //              调用方保证其存活期覆盖本对象。
    explicit ContextualToolProvider(IToolProvider& tools);

    // 根据用户输入分析并激活相关工具类别
    void updateContext(const std::string& user_input);

    // 获取当前活跃的工具定义（核心 + 已激活类别）
    std::vector<llm::ToolDefinition> getActiveDefinitions() const;

    // 强制激活某个类别（LLM 调用了未暴露工具时的兜底）
    void activateCategory(ToolCategory cat);

    // 重置为初始状态（仅核心工具）
    void reset();

    // 当前已激活的类别数
    size_t activeCategoryCount() const { return active_categories_.size(); }

private:
    IToolProvider& tools_;
    std::set<ToolCategory> active_categories_;

    bool isCoreTool(const std::string& name) const;
};

} // namespace agent
