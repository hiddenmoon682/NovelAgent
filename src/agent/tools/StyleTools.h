#pragma once

// 写作风格查询工具 — 允许 LLM 主动读取完整的 Style 配置。
//
// 当前 Style 信息在 system prompt 中被动注入，
// 此工具让 LLM 可以随时查看完整风格指南，用于：
// - 在写作前确认具体的风格约束（语调、节奏、视角等）
// - 检查禁用短语和禁用套路清单
// - 确认章节长度目标、开头/结尾风格偏好

#include "agent/tools/BuiltInTool.h"
#include <nlohmann/json_fwd.hpp>

namespace agent {

// 写作风格查询工具。
// 参数: 无
// 返回: 完整 Style 对象 JSON（24 个风格字段 + 辅助摘要字段）
class ReadStyleTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit ReadStyleTool(std::shared_ptr<Project> p) : project_(std::move(p)) {}
    std::string name() const override { return "read_style"; }
    std::string description() const override {
        return "读取当前项目的完整写作风格配置，包括语调、叙事视角、"
               "文风、对话风格、感官聚焦、章节结构偏好、禁用短语和套路清单等。"
               "在开始写作或修订前调用，确保输出符合项目风格要求。";
    }
    std::string brief() const override { return "读取写作风格配置"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Setting; }
};

// 更新写作风格配置。
// 通过 fields 白名单机制安全写入，Style 是单例无需 id。
// 参数: fields (object, required)
class UpdateStyleTool : public BuiltInTool {
    std::shared_ptr<Project> project_;
public:
    explicit UpdateStyleTool(std::shared_ptr<Project> p) : project_(std::move(p)) {}
    std::string name() const override { return "update_style"; }
    std::string description() const override {
        return "更新当前项目的写作风格配置。可修改语调、叙事视角、文风、对话密度、"
               "章节长度目标、禁用短语清单等。通过 fields 对象批量更新。";
    }
    std::string brief() const override { return "更新写作风格配置"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::Setting; }
};

} // namespace agent
