#pragma once
#include "agent/tools/BuiltInTool.h"
#include <nlohmann/json_fwd.hpp>

namespace agent {

// 执行 PowerShell 命令（Windows）。
// 参数: command (string) — PowerShell 命令
// 返回: { stdout, stderr, exit_code }
class RunPowerShellTool : public BuiltInTool {
public:
    std::string name() const override { return "run_powershell"; }
    std::string description() const override {
        return "执行只读 PowerShell 查询命令并返回结果。仅允许 Get-ChildItem/Get-Content/Select-String 等安全 cmdlet。用于文件列表、文本搜索等。";
    }
    std::string brief() const override { return "执行只读 PowerShell 查询"; }
    nlohmann::json parameters() const override;
    nlohmann::json execute(const nlohmann::json& args) override;
    ToolCategory category() const override { return ToolCategory::System; }
};

} // namespace agent
