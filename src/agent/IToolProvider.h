#pragma once

/// 工具提供者抽象接口 — 解耦工具调用方与 ToolRegistry 具体实现。
///
/// 架构改进（P0）：
/// - SubAgent 不再持有完整 ToolRegistry&，改为持有 IToolProvider&
/// - RestrictedToolProvider 在类型系统层面保证安全约束（仅暴露允许的工具）
/// - O(n×m) 过滤优化为 O(n)
///
/// 使用示例:
///   RestrictedToolProvider provider(registry, {"read_chapter", "get_character"});
///   SubAgent agent(client, provider);  // 类型安全：agent 只能调用白名单工具

#include "llm/Message.h"

#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace agent {

class ToolRegistry;

/// 工具提供者抽象接口。
class IToolProvider {
public:
    virtual ~IToolProvider() = default;

    /// 获取可用工具的定义列表（传给 LLM）。
    virtual std::vector<llm::ToolDefinition> getDefinitions() const = 0;

    /// 执行指定工具。
    /// @param name  工具名称
    /// @param args  调用参数 JSON
    /// @return      执行结果 JSON
    virtual nlohmann::json execute(const std::string& name,
                                    const nlohmann::json& args) = 0;

    /// 检查工具是否可用。
    virtual bool has(const std::string& name) const = 0;
};

/// 受限工具提供者 — 仅暴露白名单中的工具。
///
/// 安全保证：即使调用方持有 RestrictedToolProvider 引用，
/// 也无法执行白名单之外的工具。
class RestrictedToolProvider : public IToolProvider {
public:
    /// @param registry     底层完整 ToolRegistry
    /// @param allowed_names 允许的工具名列表（白名单）
    RestrictedToolProvider(ToolRegistry& registry,
                           std::vector<std::string> allowed_names);

    std::vector<llm::ToolDefinition> getDefinitions() const override;
    nlohmann::json execute(const std::string& name,
                            const nlohmann::json& args) override;
    bool has(const std::string& name) const override;

private:
    ToolRegistry& registry_;
    std::vector<std::string> allowed_;
};

} // namespace agent
