#include "agent/ContextualToolProvider.h"

#include <algorithm>
#include <unordered_set>

namespace agent {

namespace {

// 核心工具 — 始终暴露给 LLM，无需激活
const std::vector<std::string> kCoreTools = {
    "read_chapter",
    "write_chapter",
    "append_to_chapter",
    "list_chapters",
    "get_latest_chapter",
    "get_outline",
    "get_project_status",
    "get_chapter_context",
};

// 关键词 → 类别映射（用户输入包含关键词时激活对应类别）
struct KeywordRule {
    const char* keyword;
    ToolCategory category;
};

const std::vector<KeywordRule> kKeywordRules = {
    {"角色", ToolCategory::Character},
    {"人物", ToolCategory::Character},
    {"character", ToolCategory::Character},
    {"关系", ToolCategory::Character},
    {"成长", ToolCategory::Character},

    {"设定", ToolCategory::Setting},
    {"setting", ToolCategory::Setting},
    {"场景", ToolCategory::Setting},

    {"世界", ToolCategory::WorldRule},
    {"规则", ToolCategory::WorldRule},
    {"world", ToolCategory::WorldRule},

    {"大纲", ToolCategory::Outline},
    {"卷", ToolCategory::Outline},
    {"情节", ToolCategory::Outline},
    {"outline", ToolCategory::Outline},
    {"线索", ToolCategory::Outline},

    {"风格", ToolCategory::Content},
    {"文风", ToolCategory::Content},
    {"style", ToolCategory::Content},

    {"搜索", ToolCategory::System},
    {"记忆", ToolCategory::System},
    {"search", ToolCategory::System},
    {"命令", ToolCategory::System},
    {"执行", ToolCategory::System},
    {"shell", ToolCategory::System},
};

} // namespace

ContextualToolProvider::ContextualToolProvider(ToolRegistry& registry)
    : registry_(registry) {}

void ContextualToolProvider::updateContext(const std::string& user_input) {
    for (const auto& rule : kKeywordRules) {
        if (user_input.find(rule.keyword) != std::string::npos) {
            active_categories_.insert(rule.category);
        }
    }
}

std::vector<llm::ToolDefinition>
ContextualToolProvider::getActiveDefinitions() const {
    // 构建允许的工具名集合：核心工具 + 已激活类别的工具
    std::unordered_set<std::string> allowed(kCoreTools.begin(), kCoreTools.end());
    for (auto cat : active_categories_) {
        auto names = registry_.toolNamesByCategory(cat);
        allowed.insert(names.begin(), names.end());
    }

    auto all = registry_.getToolDefinitions();
    std::vector<llm::ToolDefinition> result;
    result.reserve(all.size());

    for (auto& def : all) {
        if (allowed.count(def.name))
            result.push_back(std::move(def));
    }
    return result;
}

void ContextualToolProvider::activateCategory(ToolCategory cat) {
    active_categories_.insert(cat);
}

void ContextualToolProvider::reset() {
    active_categories_.clear();
}

bool ContextualToolProvider::isCoreTool(const std::string& name) const {
    return std::find(kCoreTools.begin(), kCoreTools.end(), name) != kCoreTools.end();
}

} // namespace agent
