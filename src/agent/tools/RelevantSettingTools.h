#pragma once

#include "agent/tools/BuiltInTool.h"
#include <nlohmann/json_fwd.hpp>

namespace agent {

/// 获取与指定章节最相关的世界观设定/地点的完整信息。
class GetRelevantSettingsTool : public BuiltInTool {
    std::shared_ptr<ProjectAccess> project_;
public:
    explicit GetRelevantSettingsTool(std::shared_ptr<ProjectAccess> p) : project_(p) {}
    std::string name() const override { return "get_relevant_settings"; }
    std::string description() const override {
        return "获取与指定章节最相关的世界观设定/地点的完整信息。设定按关联系数排序，"
               "确保容量有限时优先返回与本章场景直接相关的设定。";
    }
    std::string brief() const override { return "获取本章相关设定"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Setting; }
    bool isReadOnly() const override { return true; }
};

} // namespace agent
