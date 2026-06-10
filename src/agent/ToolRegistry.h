#pragma once

#include "agent/IToolProvider.h"
#include "agent/tools/BuiltInTool.h"
#include "llm/Message.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace agent {

/// 工具注册中心 — 实现 IToolProvider 接口。
///
/// 支持两种注册方式：
///   1. registerTool() — 函数式（轻量、无状态工具），直接传入 lambda
///   2. registerBuiltInTool() — 类式（复杂、有状态工具），传入 BuiltInTool 子类实例
///
/// 使用示例：
///   ToolRegistry registry;
///   registry.registerTool("echo", "回显输入", schema, ToolCategory::System,
///       [](const json& args) { return args; });
///
///   auto tools = registry.getToolDefinitions();  // 传给 LLMClient::chat()
///   auto result = registry.executeTool("echo", {{"msg", "hello"}});
class ToolRegistry : public IToolProvider {
public:
    ToolRegistry() = default;
    ~ToolRegistry() = default;

    // ================================================================
    // 注册
    // ================================================================

    /// 注册函数式工具（轻量、无状态）。
    /// @param name         工具名称（与 LLM function calling name 一致）
    /// @param description  工具功能描述
    /// @param parameters   参数 JSON Schema
    /// @param category     工具类别
    /// @param fn           执行回调：接收 args JSON，返回 result JSON
    void registerTool(std::string name,
                      std::string description,
                      const nlohmann::json& parameters,
                      ToolCategory category,
                      std::function<nlohmann::json(const nlohmann::json&)> fn);

    /// 注册内置工具类实例（复杂、有状态）。
    /// 内部将 BuiltInTool::execute 包装为函数式回调。
    void registerBuiltInTool(std::unique_ptr<BuiltInTool> tool);

    // ================================================================
    // 查询
    // ================================================================

    /// 获取所有已注册工具的定义列表（直接传给 LLMClient::chat）。
    std::vector<llm::ToolDefinition> getToolDefinitions() const;

    /// 检查指定名称的工具是否已注册。
    bool hasTool(const std::string& name) const;

    /// 已注册工具总数。
    size_t toolCount() const { return tools_.size(); }

    // ================================================================
    // 执行
    // ================================================================

    /// 执行指定工具。
    /// @param name  工具名称
    /// @param args  调用参数（LLM 传来的 arguments JSON）
    /// @return      工具执行结果 JSON
    ///              若工具不存在，返回 {"error": "..."}
    ///              若执行抛异常，返回 {"error": "..."}
    nlohmann::json executeTool(const std::string& name,
                                const nlohmann::json& args);

    // ================================================================
    // 分组查询（供 /help 等命令使用）
    // ================================================================

    /// 获取所有工具名称列表。
    std::vector<std::string> toolNames() const;

    /// 按类别获取工具名称列表。
    std::vector<std::string> toolNamesByCategory(ToolCategory category) const;

    // ================================================================
    // IToolProvider 接口实现
    // ================================================================

    std::vector<llm::ToolDefinition> getDefinitions() const override {
        return getToolDefinitions();
    }
    nlohmann::json execute(const std::string& name, const nlohmann::json& args) override {
        return executeTool(name, args);
    }
    bool has(const std::string& name) const override {
        return hasTool(name);
    }

private:
    /// 内部统一的工具条目（函数式和类式最终都归为此结构）
    struct ToolEntry {
        std::string name;
        std::string description;
        nlohmann::json parameters;
        ToolCategory category;
        std::function<nlohmann::json(const nlohmann::json&)> fn;
    };

    std::vector<ToolEntry> tools_;
    std::vector<std::unique_ptr<BuiltInTool>> builtin_instances_; // 持有类式工具的所有权

    /// 按名称查找工具条目，未找到返回 nullptr。
    const ToolEntry* findTool(const std::string& name) const;
};

} // namespace agent
