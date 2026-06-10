#pragma once

#include "llm/Message.h"
#include <nlohmann/json_fwd.hpp>  // 仅前向声明，避免拉入 ~25K 行模板
#include <functional>
#include <memory>  // shared_ptr, unique_ptr
#include <string>
#include <vector>

struct Project; // 全局 Project 前向声明

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

// ============================================================================
// BuiltInTool — 内置工具抽象基类（含自注册机制）
// ============================================================================

class BuiltInTool {
public:
    virtual ~BuiltInTool() = default;

    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual nlohmann::json parameters() const = 0;
    virtual nlohmann::json execute(const nlohmann::json& args) = 0;
    virtual ToolCategory category() const = 0;

    llm::ToolDefinition toDefinition() const {
        return llm::ToolDefinition{name(), description(), parameters()};
    }

    // ================================================================
    // 自注册机制 — 工具只需在 .cpp 中调用 REGISTER_TOOL 宏
    // ================================================================

    using Factory = std::function<std::unique_ptr<BuiltInTool>(std::shared_ptr<Project>)>;

    /// 注册工具工厂（由 REGISTER_TOOL 宏调用）
    static void registerFactory(std::string name, Factory factory);

    /// 将所有已注册工具实例化并添加到 ToolRegistry
    static void registerAllTo(class ToolRegistry& registry,
                               std::shared_ptr<Project> project,
                               const std::vector<std::string>& disabled = {});

    /// 列出所有已注册的工具名
    static const std::vector<std::string>& registeredToolNames();

private:
    struct Entry { std::string name; Factory factory; };
    static std::vector<Entry>& factories();
};

} // namespace agent

/// 在工具 .cpp 文件中使用此宏实现自注册。
/// 示例: REGISTER_TOOL(ReadChapterTool, "read_chapter", read_chapter)
#define REGISTER_TOOL(ToolClass, toolName, varSuffix) \
    namespace { \
        static const bool _reg_##varSuffix = []() { \
            agent::BuiltInTool::registerFactory( \
                toolName, \
                [](std::shared_ptr<::Project> p) -> std::unique_ptr<agent::BuiltInTool> { \
                    return std::make_unique<ToolClass>(std::move(p)); \
                }); \
            return true; \
        }(); \
    }
