#pragma once

/// REPL 处理器 — Phase 5 增强版。
///
/// 支持 Claude Code 风格体验：
/// - 直接输入 novelagent.exe 进入交互 GUI（无需 -p）
/// - /new <name> 创建新项目
/// - /load <path> 打开已有项目
/// - 彩色状态栏 + ANSI 主题

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

    /// 切换当前项目（供 /load /new 使用）。
    void setProject(std::shared_ptr<Project> p);

private:
    agent::Agent& agent_;
    IOutputChannel& out_;
    CommandParser parser_;
    TerminalGUI gui_;
    std::shared_ptr<Project> project_;
    std::string welcome_;

    void setupCommands();
    void setupPhase5Commands();

    /// 打开或创建项目并刷新 Agent 工具注册。
    bool openProject(const std::string& path);

    std::vector<std::string> getCompletions(const std::string& prefix) const;
    void showCompletions(const std::vector<std::string>& completions) const;
    void autoSaveOnError();
};
