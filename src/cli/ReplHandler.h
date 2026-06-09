#pragma once

/// REPL 处理器 — Phase 5 增强版。
///
/// 新增:
/// - TerminalGUI 集成（颜色主题/状态栏/进度指示器）
/// - Tab 补全（斜杠命令名）
/// - /status /config /export /trace 命令
/// - 错误恢复（自动保存 + 优雅降级）

#include "agent/Agent.h"
#include "cli/CommandParser.h"
#include "cli/IOutputChannel.h"
#include "cli/TerminalGUI.h"

#include <memory>
#include <string>
#include <vector>

struct Project;

class ReplHandler {
public:
    ReplHandler(agent::Agent& agent, IOutputChannel& out,
                std::shared_ptr<Project> project = nullptr);

    void run();
    void setWelcomeMessage(std::string msg);

private:
    agent::Agent& agent_;
    IOutputChannel& out_;
    CommandParser parser_;
    TerminalGUI gui_;
    std::shared_ptr<Project> project_;
    std::string welcome_;

    void setupCommands();
    void setupPhase5Commands();

    /// 自动补全候选列表。
    std::vector<std::string> getCompletions(const std::string& prefix) const;

    /// 显示补全提示。
    void showCompletions(const std::vector<std::string>& completions) const;

    /// 错误恢复：自动保存项目。
    void autoSaveOnError();
};
