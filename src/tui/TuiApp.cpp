/// TuiApp 实现 — FTXUI 终端界面主控制器。

#include "tui/TuiApp.h"

#include "agent/Agent.h"
#include "llm/ILLMClient.h"
#include "project/Models.h"

#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/event.hpp>
#include <spdlog/spdlog.h>

using namespace ftxui;

TuiApp::TuiApp(agent::Agent& agent, std::shared_ptr<Project> project)
    : agent_(agent)
    , project_(project ? std::move(project) : std::make_shared<Project>())
    , screen_(ScreenInteractive::Fullscreen())
{
    // 初始化侧边栏数据
    sidebar_.setProject(project_);

    // 初始化状态栏
    if (project_ && !project_->title.empty()) {
        int chCount = static_cast<int>(project_->outline.chapters.size());
        int charCount = static_cast<int>(project_->characters.size());
        statusBar_.setProjectInfo(project_->title, chCount, charCount);
    }
}

void TuiApp::run() {
    // ── 构建组件树（一次性创建，后续 render 复用状态）──

    // 输入栏：需保持状态（文本缓冲区、历史），只创建一次
    auto inputComp = inputBar_.render();

    // 设置输入提交回调
    inputBar_.onSubmit([this](const std::string& text) {
        processUserInput(text);
    });

    // 顶层组件：垂直布局
    auto topLevel = Container::Vertical({
        inputComp,
    });

    // 顶层渲染器：组装所有 Element（Claude Code 风格 — 无重边框）
    auto mainRenderer = Renderer(topLevel, [this, inputComp] {
        return vbox({
            // 顶部状态栏（单行暗色）
            statusBar_.render() | bgcolor(Color::Grey15),
            // 主区域：聊天面板 + 侧边栏
            hbox({
                chatPanel_.render() | flex_grow,
                separator() | color(Color::Grey30),
                sidebar_.render(),
            }) | flex_grow,
            // 底部分隔线
            separator() | color(Color::Grey30),
            // 底部输入栏（左对齐，不无限扩展）
            hbox({
                text(" > ") | color(Color::Cyan) | bold,
                inputComp->Render() | xflex_grow,
            }) | size(HEIGHT, EQUAL, 1),
        });
    });

    // 捕获 Ctrl+D / Escape 退出
    auto component = CatchEvent(mainRenderer, [this](Event event) {
        if (event == Event::Escape || event == Event::CtrlD) {
            screen_.Exit();
            return true;
        }
        return false;
    });

    // 进入 FTXUI 事件循环
    try {
        screen_.Loop(component);
    } catch (const std::exception& e) {
        spdlog::error("[TUI] 异常退出: {}", e.what());
    }

    stopWorker();
}

void TuiApp::processUserInput(const std::string& input) {
    if (input.empty()) return;

    // 斜杠命令
    if (!input.empty() && input[0] == '/') {
        if (handleCommand(input)) return;
    }

    // 显示用户消息
    chatPanel_.appendUserMessage(input);

    // 在 worker 线程中执行 Agent 调用
    executeAgent(input);
}

bool TuiApp::handleCommand(const std::string& input) {
    // 解析命令
    std::string cmd = input;
    std::string args;
    auto spacePos = input.find(' ');
    if (spacePos != std::string::npos) {
        cmd = input.substr(0, spacePos);
        args = input.substr(spacePos + 1);
    }

    if (cmd == "/exit" || cmd == "/quit" || cmd == "/q") {
        screen_.Exit();
        return true;
    }

    if (cmd == "/help" || cmd == "/?") {
        chatPanel_.appendSystemMessage(
            "可用命令:\n"
            "  /help     — 显示此帮助\n"
            "  /exit     — 退出 TUI\n"
            "  /status   — 项目状态\n"
            "  /save     — 保存项目\n"
            "  /export   — 导出 Markdown\n"
            "  /clear    — 清空对话\n"
            "  /tools    — 列出可用工具"
        );
        return true;
    }

    if (cmd == "/status") {
        if (project_ && !project_->title.empty()) {
            auto status = "项目: " + project_->title + "\n"
                        + "章节: " + std::to_string(project_->outline.chapters.size()) + "\n"
                        + "角色: " + std::to_string(project_->characters.size());
            chatPanel_.appendSystemMessage(status);
        } else {
            chatPanel_.appendSystemMessage("（无打开项目）");
        }
        return true;
    }

    if (cmd == "/clear") {
        chatPanel_ = TuiChatPanel();
        chatPanel_.appendSystemMessage("对话已清空。");
        return true;
    }

    // 未知命令
    chatPanel_.appendError("未知命令: " + cmd + "（输入 /help 查看可用命令）");
    return true;
}

void TuiApp::executeAgent(const std::string& input) {
    if (agent_running_) {
        chatPanel_.appendError("Agent 正在处理中，请等待…");
        return;
    }

    agent_running_ = true;
    statusBar_.setMode("思考中");

    // 为当前的助手回复创建新消息
    chatPanel_.startAssistantMessage();

    // ── 构建 StreamCallbacks（Claude Code 风格 UI 反馈）──
    llm::StreamCallbacks cb;
    cb.on_content = [this](const std::string& delta) {
        screen_.Post([this, delta] {
            chatPanel_.appendContent(delta);
        });
    };
    cb.on_reasoning = [this](const std::string& delta) {
        screen_.Post([this, delta] {
            chatPanel_.appendThinking(delta);
        });
    };
    cb.on_tool_call_start = [this] {
        screen_.Post([this] {
            statusBar_.setMode("执行工具");
            chatPanel_.appendToolCall("工具调用");
        });
    };
    cb.on_complete = [this](const llm::LLMResponse& resp) {
        screen_.Post([this, resp] {
            chatPanel_.finishMessage();
            statusBar_.updateTokens(resp.total_tokens);
            statusBar_.setMode("就绪");
            agent_running_ = false;
            sidebar_.refresh();
        });
    };
    cb.on_error = [this](const std::string& err) {
        screen_.Post([this, err] {
            chatPanel_.appendError(err);
            statusBar_.setMode("错误");
            agent_running_ = false;
        });
    };

    // ── 在 worker 线程执行 ──
    stopWorker();
    worker_thread_ = std::thread([this, input, cb = std::move(cb)]() {
        try {
            agent_.processUserMessage(input, cb);
        } catch (const std::exception& e) {
            screen_.Post([this, msg = std::string(e.what())] {
                chatPanel_.appendError("Agent 异常: " + msg);
                statusBar_.setMode("错误");
                agent_running_ = false;
            });
        }
    });
    worker_thread_.detach();
}

void TuiApp::stopWorker() {
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}
