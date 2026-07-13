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

    // 注册工具工厂（由 REGISTER_TOOL 宏调用）
    static void registerFactory(std::string name, Factory factory);

    // 将所有已注册工具实例化并添加到 ToolRegistry
    static void registerAllTo(class ToolRegistry& registry,
                               std::shared_ptr<Project> project,
                               const std::vector<std::string>& disabled = {});

    // 列出所有已注册的工具名
    static const std::vector<std::string>& registeredToolNames();

private:
    // 注册条目：工具名 + 工厂函数
    // 每个工具在 main() 前通过 REGISTER_TOOL 宏向 factories() 存入一个 Entry。
    // name    工具名称（如 "read_chapter"），用于去重和禁用检查
    // factory 接受 Project 指针，返回 unique_ptr<BuiltInTool>
    struct Entry { std::string name; Factory factory; };

    // 全局工厂容器（Meyers' Singleton，函数内 static 局部变量）
    // 返回对所有已注册工厂的引用。
    // 生命周期：首次调用时创建，程序结束时销毁。
    // 线程安全性：动态初始化阶段（main() 前）为单线程，无需加锁。
    static std::vector<Entry>& factories();
};

} // namespace agent

// 在工具 .cpp 文件中使用此宏实现自注册。
// 示例: REGISTER_TOOL(ReadChapterTool, "read_chapter", read_chapter)
//
// ⚠️ 静态初始化顺序限制（Issue 19）：
// 此宏在文件作用域创建 static const bool 变量，利用 C++ 动态初始化阶段
// （main() 之前）执行 lambda 完成注册。不同编译单元（.cpp 文件）之间的
// 动态初始化顺序是未定义的（C++ 标准 3.6.2）。
//
// 因此工具构造函数 **不得** 依赖其他工具已注册的状态（如调用
// BuiltInTool::registeredToolNames() 查找其他工具）。当前所有工具
// 构造函数仅接受 std::shared_ptr<Project> 参数，无交叉依赖，安全。
// 未来新增工具时请保持此约束。
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

// 对于不需要 Project 指针的工具，使用此宏注册。
// 示例: REGISTER_TOOL_NP(RunPowerShellTool, "run_powershell", run_powershell)
//
// 与 REGISTER_TOOL 的区别：工厂 lambda 中构造工具时不传入 Project 参数。
// 适用于构造函数签名为 T()（无参数）的内置工具。
// 静态初始化顺序限制与 REGISTER_TOOL 相同（见上方注释）。
#define REGISTER_TOOL_NP(ToolClass, toolName, varSuffix) \
    namespace { \
        static const bool _reg_##varSuffix = []() { \
            agent::BuiltInTool::registerFactory( \
                toolName, \
                [](std::shared_ptr<::Project>) -> std::unique_ptr<agent::BuiltInTool> { \
                    return std::make_unique<ToolClass>(); \
                }); \
            return true; \
        }(); \
    }
