#pragma once

#include "agent/tools/BuiltInTool.h"
#include <nlohmann/json_fwd.hpp>

namespace agent {

/// 获取编写本章节所需的核心上下文（元数据 + 卷 + 关联剧情线）。
class GetChapterContextTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit GetChapterContextTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "get_chapter_context"; }
    std::string description() const override {
        return "获取指定章节的创作上下文快照，包括章节元数据、归属卷的信息、"
               "以及当前活跃的剧情线列表。在开始写作某章前调用此工具获取最核心的上下文信息。";
    }
    std::string brief() const override { return "获取章节核心上下文"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Outline; }
};

} // namespace agent
