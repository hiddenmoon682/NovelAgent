#pragma once

#include "llm/Message.h"
#include <nlohmann/json.hpp>
#include <string>

namespace agent {

// ============================================================================
// ToolCategory — 工具类别（用于分组展示和 /help 命令）
// ============================================================================

enum class ToolCategory {
    Project,    // 项目管理（创建/打开/保存）
    Content,    // 内容读写（章节读写）
    Character,  // 角色管理
    Setting,    // 设定管理
    Outline,    // 大纲管理
    WorldRule,  // 世界规则
    System      // 系统操作（Shell、文件）
};

/// 工具类别 → 中文名称（供 /help 展示）
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

// ============================================================================
// BuiltInTool — 内置工具抽象基类
// ============================================================================

/// 每个具体工具继承此类，实现 5 个纯虚方法。
/// ToolRegistry 同时支持两种注册方式：
///   1. registerTool() — 函数式（轻量、无状态工具）
///   2. registerBuiltInTool() — 类式（复杂、有状态工具，继承此类）
class BuiltInTool {
public:
    virtual ~BuiltInTool() = default;

    /// 工具名称（与 LLM function calling 中的 name 一致）
    virtual std::string name() const = 0;

    /// 工具功能描述（告诉 LLM 何时调用此工具）
    virtual std::string description() const = 0;

    /// 参数 JSON Schema（OpenAI function calling 格式的 parameters 字段）
    virtual nlohmann::json parameters() const = 0;

    /// 执行工具。
    /// @param args  LLM 传来的参数 JSON（对应 schema 中定义的字段）
    /// @return      结果 JSON（可以是任意结构，LLM 会自行解读）
    virtual nlohmann::json execute(const nlohmann::json& args) = 0;

    /// 工具所属类别
    virtual ToolCategory category() const = 0;

    /// 转换为 ToolDefinition，供 LLMClient::chat() 作为 tools 参数传入。
    llm::ToolDefinition toDefinition() const {
        return llm::ToolDefinition{name(), description(), parameters()};
    }
};

} // namespace agent
