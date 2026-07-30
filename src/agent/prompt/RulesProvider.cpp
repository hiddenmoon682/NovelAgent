#include "agent/prompt/RulesProvider.h"

#include "utils/FileUtils.h"

namespace agent::prompt {

RulesProvider::RulesProvider(std::string config_dir)
    : config_dir_(std::move(config_dir))
{
}

std::string RulesProvider::combined(const std::string& project_path) const
{
    const std::string global_rules =
        readFile(utils::file::joinPath(config_dir_, "rules.md"));

    std::string project_rules;
    if (!project_path.empty()) {
        project_rules = readFile(utils::file::joinPath(
            project_path, ".novelagent/rules.md"));
    }

    // 叠加顺序固定：全局在前、项目在后（项目规则可针对单项目补充/细化全局约束）
    std::string result;
    if (!global_rules.empty())
        result += "## 全局规则\n\n" + global_rules;
    if (!project_rules.empty()) {
        if (!result.empty())
            result += "\n\n";
        result += "## 项目规则\n\n" + project_rules;
    }
    return result;
}

std::string RulesProvider::readFile(const std::string& path)
{
    return utils::file::readText(path);  // 打不开时返回空串，无异常
}

} // namespace agent::prompt
