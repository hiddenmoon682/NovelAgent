#pragma once

/// 降级管线 — 管理上下文压缩的多级策略。
///
/// Phase 4 架构改进：从 ContextManager 的 if-else 链重构为策略模式。
/// 新增降级策略只需实现 IDegradationStrategy 并注册，符合开闭原则。

#include "agent/ContextManagerTypes.h"

#include <memory>
#include <string>
#include <vector>

namespace agent {

/// 降级策略抽象接口。
class IDegradationStrategy {
public:
    virtual ~IDegradationStrategy() = default;

    /// 返回此策略对应的降级等级。
    virtual DegradationLevel level() const = 0;

    /// 应用降级，返回压缩后的文本。
    virtual std::string apply(const std::string& prompt) = 0;

    /// 估算的 token 节省比例（0.0-1.0），用于自动选择策略。
    virtual double estimatedSavingRatio() const = 0;
};

/// L1 — 截断当前章节内容到末尾 ~2000 字。
class TruncateChapterStrategy : public IDegradationStrategy {
public:
    DegradationLevel level() const override { return DegradationLevel::TruncateChapter; }
    std::string apply(const std::string& prompt) override;
    double estimatedSavingRatio() const override { return 0.15; }
};

/// L2 — 移除角色详细档案，仅保留名称和角色类型。
class RemoveCharacterDetailsStrategy : public IDegradationStrategy {
public:
    DegradationLevel level() const override { return DegradationLevel::RemoveDetails; }
    std::string apply(const std::string& prompt) override;
    double estimatedSavingRatio() const override { return 0.30; }
};

/// L3 — 移除相邻章节大纲。
class RemoveAdjacentChaptersStrategy : public IDegradationStrategy {
public:
    DegradationLevel level() const override { return DegradationLevel::RemoveAdjacent; }
    std::string apply(const std::string& prompt) override;
    double estimatedSavingRatio() const override { return 0.45; }
};

/// L4 — 标记对话截断（实际截断在 truncateMessages 中执行）。
class TruncateConversationStrategy : public IDegradationStrategy {
public:
    DegradationLevel level() const override { return DegradationLevel::TruncateConv; }
    std::string apply(const std::string& prompt) override;
    double estimatedSavingRatio() const override { return 0.60; }
};

/// L5 — 全文压缩为最短摘要。
class SummarizeStrategy : public IDegradationStrategy {
public:
    DegradationLevel level() const override { return DegradationLevel::Summarize; }
    std::string apply(const std::string& prompt) override;
    double estimatedSavingRatio() const override { return 0.80; }
};

/// 降级管线 — 管理策略注册和执行。
///
/// 使用方式：
///   DegradationPipeline pipeline;
///   pipeline.registerDefaultStrategies();  // 注册内置 5 级策略
///   auto level = pipeline.determineLevel(required, budget);
///   auto result = pipeline.execute(prompt, level);
class DegradationPipeline {
public:
    DegradationPipeline() = default;

    /// 注册内置 5 级降级策略。
    void registerDefaultStrategies();

    /// 注册自定义降级策略（替换或新增）。
    void registerStrategy(std::unique_ptr<IDegradationStrategy> strategy);

    /// 确定需要触发的降级等级。
    /// @param required_tokens  需要的 token 数
    /// @param available_budget 可用预算
    /// @return                 最低有效降级等级
    DegradationLevel determineLevel(int required_tokens, int available_budget) const;

    /// 执行指定等级的降级（自动应用该等级及所有更低等级的策略）。
    std::string execute(const std::string& prompt, DegradationLevel level) const;

private:
    std::vector<std::unique_ptr<IDegradationStrategy>> strategies_;
};

} // namespace agent
