#pragma once

#include "agent/tools/BuiltInTool.h"
#include <nlohmann/json_fwd.hpp>

namespace agent {

// 获取当前最新章节的基本信息。
// 通过扫描 project_->outline.chapters 中 order 最大的章节来确定。
// LLM 无需自己追踪章节 ID，在开始写作前调用此工具即可了解当前进度。
class GetLatestChapterTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit GetLatestChapterTool(std::shared_ptr<Project> p) : project_(p) {}
    std::string name() const override { return "get_latest_chapter"; }
    std::string description() const override {
        return "获取当前最新章节的基本信息，包括顺序号、标题、状态和概要。"
               "在开始写作前调用此工具了解当前进度，无需自行追踪章节 ID。";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Outline; }
};

} // namespace agent
