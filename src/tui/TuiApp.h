#pragma once

/// FTXUI TUI 应用主控 — 类似 Claude Code 的终端界面。
///
/// 职责：
/// - 管理 FTXUI ScreenInteractive（全屏交互）
/// - 组装组件树（ChatPanel / InputBar / StatusBar / Sidebar）
/// - 事件循环（FTXUI Loop）
/// - Worker 线程调度（Agent 调用在后台线程，UI 不冻结）
/// - 斜杠命令路由（通过 CommandParser）

#include "tui/TuiChatPanel.h"
#include "tui/TuiInputBar.h"
#include "tui/TuiStatusBar.h"
#include "tui/TuiSidebar.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <functional>
#include <memory>
#include <string>
#include <thread>

// 前向声明
namespace agent { class Agent; }
struct Project;

/// TUI 应用程序主类。
/// 使用方式：
///   TuiApp app(agent, project);
///   app.run();  // 阻塞直到用户退出
class TuiApp {
public:
    /// @param agent   Agent 实例（外部持有生命周期）
    /// @param project 小说项目（可为空）
    TuiApp(agent::Agent& agent, std::shared_ptr<Project> project);

    /// 进入 FTXUI 事件循环（阻塞）。
    void run();

private:
    agent::Agent& agent_;
    std::shared_ptr<Project> project_;

    // FTXUI 组件
    ftxui::ScreenInteractive screen_;
    TuiChatPanel chatPanel_;
    TuiInputBar inputBar_;
    TuiStatusBar statusBar_;
    TuiSidebar sidebar_;

    // 线程管理
    std::thread worker_thread_;
    bool agent_running_ = false;

    /// 处理用户输入：斜杠命令 → 本地处理，普通文本 → Agent。
    void processUserInput(const std::string& input);

    /// 在 worker 线程中执行 Agent 调用并桥接 StreamCallbacks。
    void executeAgent(const std::string& input);

    /// 处理斜杠命令（/exit, /help, /status, /save, /export 等）。
    bool handleCommand(const std::string& input);

    /// 停止 worker 线程。
    void stopWorker();
};
