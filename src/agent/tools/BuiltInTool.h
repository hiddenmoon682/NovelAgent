#pragma once

#include "agent/tool/IToolProvider.h"
#include "llm/Message.h"
#include <nlohmann/json_fwd.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct Project;
namespace retrieval {
class IVectorStore;
class IEmbeddingGenerator;
}
namespace skill {
class SkillRegistry;
}

namespace agent {

class LongTermMemoryStore;

// ============================================================================
// ToolDependencies — 工具构造所需的共享依赖集合
// 工具的工厂函数在构造时按需取用其中的成员；未用到的成员可为 nullptr。
// ============================================================================

struct ToolDependencies {
    std::shared_ptr<Project> project;              // 当前小说项目（工具读写项目数据）。
                                                   // 共享所有权：工具与 App 共享同一项目实例
    retrieval::IVectorStore* vector_store = nullptr;          // 语义检索向量库（RAG 查询）
    retrieval::IEmbeddingGenerator* embedding_gen = nullptr;  // 文本嵌入生成器（写入向量库前向量化）
    LongTermMemoryStore* memory_store = nullptr;              // 长期记忆日志（save_memory 等记忆工具，可选）
    skill::SkillRegistry* skill_registry = nullptr;           // 技能注册表（use_skill/save_skill 等技能工具）
};

// ============================================================================
// BuiltInTool — 内置工具抽象基类（含自注册机制）
// ============================================================================

class BuiltInTool {
public:
    virtual ~BuiltInTool() = default;

    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    // 极简描述（一句话），用于延迟工具存根注入 system prompt。
    // 默认回退到 description()，工具可覆写提供更短的版本。
    virtual std::string brief() const { return description(); }
    virtual nlohmann::json parameters() const = 0;
    virtual nlohmann::json execute(const nlohmann::json& args) = 0;
    virtual ToolCategory category() const = 0;

    llm::ToolDefinition toDefinition() const {
        return llm::ToolDefinition{name(), description(), parameters()};
    }

    // ================================================================
    // 自注册机制 — 工具只需在 .cpp 中调用 REGISTER_TOOL 宏
    // ================================================================

    using Factory = std::function<std::unique_ptr<BuiltInTool>(const ToolDependencies&)>;

    static void registerFactory(std::string name, Factory factory);

    static void registerAllTo(class ToolRegistry& registry,
                               const ToolDependencies& deps);

    static const std::vector<std::string>& registeredToolNames();

private:
    struct Entry { std::string name; Factory factory; };
    static std::vector<Entry>& factories();
};

} // namespace agent

// ============================================================================
// 注册宏
// ============================================================================

// 需要 Project 的工具。构造函数签名: T(std::shared_ptr<Project>)
#define REGISTER_TOOL(ToolClass, toolName, varSuffix) \
    namespace { \
        static const bool _reg_##varSuffix = []() { \
            agent::BuiltInTool::registerFactory( \
                toolName, \
                [](const agent::ToolDependencies& deps) -> std::unique_ptr<agent::BuiltInTool> { \
                    return std::make_unique<ToolClass>(deps.project); \
                }); \
            return true; \
        }(); \
    }

// 无依赖的工具。构造函数签名: T()
#define REGISTER_TOOL_NP(ToolClass, toolName, varSuffix) \
    namespace { \
        static const bool _reg_##varSuffix = []() { \
            agent::BuiltInTool::registerFactory( \
                toolName, \
                [](const agent::ToolDependencies&) -> std::unique_ptr<agent::BuiltInTool> { \
                    return std::make_unique<ToolClass>(); \
                }); \
            return true; \
        }(); \
    }

// 需要完整 ToolDependencies 的工具。构造函数签名: T(const agent::ToolDependencies&)
#define REGISTER_TOOL_DEPS(ToolClass, toolName, varSuffix) \
    namespace { \
        static const bool _reg_##varSuffix = []() { \
            agent::BuiltInTool::registerFactory( \
                toolName, \
                [](const agent::ToolDependencies& deps) -> std::unique_ptr<agent::BuiltInTool> { \
                    return std::make_unique<ToolClass>(deps); \
                }); \
            return true; \
        }(); \
    }
