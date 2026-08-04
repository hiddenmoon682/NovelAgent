#pragma once

// 工具提供者抽象接口 — 解耦工具调用方与 ToolRegistry 具体实现。
//
// Agent 通过注入 IToolProvider& 使用工具，不持有具体实现。
// RestrictedToolProvider 在类型系统层面保证安全约束（仅暴露允许的工具）。

#include "llm/Message.h"

#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace agent {

// ============================================================================
// ToolCategory — 工具类别
// ============================================================================

enum class ToolCategory {
    Project, Character, Content, Setting, Outline, WorldRule, System
};

inline const char* toolCategoryName(ToolCategory cat) {
    switch (cat) {
        case ToolCategory::Project:   return "项目管理";
        case ToolCategory::Content:   return "内容读写";
        case ToolCategory::Character: return "角色管理";
        case ToolCategory::Setting:   return "设定管理";
        case ToolCategory::Outline:   return "大纲管理";
        case ToolCategory::WorldRule: return "世界规则";
        case ToolCategory::System:    return "系统操作";
    }
    return "未知";
}

// 工具提供者抽象接口。
class IToolProvider {
public:
    virtual ~IToolProvider() = default;

    // 获取可用工具的定义列表（传给 LLM）。
    virtual std::vector<llm::ToolDefinition> getDefinitions() const = 0;

    // 执行指定工具。
    virtual nlohmann::json execute(const std::string& name,
                                    const nlohmann::json& args) = 0;

    // 检查工具是否可用。
    virtual bool has(const std::string& name) const = 0;

    // 按类别获取工具名称列表。
    virtual std::vector<std::string> toolNamesByCategory(ToolCategory category) const = 0;

    // 工具是否只读（E7/P4）：默认按名称前缀启发式；具体提供者可覆盖（如 ToolRegistry 用显式标记）。
    virtual bool isReadOnly(const std::string& name) const;
    // 默认前缀启发式（get_/list_/read_/search_）。
    static bool defaultIsReadOnly(const std::string& name);
};

// 受限工具提供者 — 仅暴露白名单中的工具。
class RestrictedToolProvider : public IToolProvider {
public:
    // 构造受限视图。
    // @param parent 底层完整工具提供者；非拥有引用，
    //               调用方保证其存活期覆盖本对象。
    // @param allowed_names 白名单：仅这些名称的工具对外可见/可执行。
    RestrictedToolProvider(IToolProvider& parent,
                           std::vector<std::string> allowed_names);

    std::vector<llm::ToolDefinition> getDefinitions() const override;
    nlohmann::json execute(const std::string& name,
                            const nlohmann::json& args) override;
    bool has(const std::string& name) const override;
    std::vector<std::string> toolNamesByCategory(ToolCategory category) const override;
    bool isReadOnly(const std::string& name) const override;

private:
    IToolProvider& parent_;
    std::vector<std::string> allowed_;
};

} // namespace agent
