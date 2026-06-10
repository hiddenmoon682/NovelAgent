#pragma once
#include "agent/tools/BuiltInTool.h"
#include <nlohmann/json.hpp>

namespace agent {

/// 执行 PowerShell 命令（Windows）。
/// 参数: command (string) — PowerShell 命令
/// 返回: { stdout, stderr, exit_code }
class RunPowerShellTool : public BuiltInTool {
public:
    std::string name() const override { return "run_powershell"; }
    std::string description() const override {
        return "执行 PowerShell 命令并返回结果（stdout/stderr/exit_code）。用于文件操作、系统查询等。";
    }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::System; }
};

} // namespace agent
