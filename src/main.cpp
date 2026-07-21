// NovelAgent — AI 写小说助手。Phase 6 前后端分离版。
// 三种模式: 单体REPL | 后端Server | 单次命令
#include "NovelAgentApp.h"
#include "agent/AgentSetup.h"
#include "cli/AnsiTerminal.h"
#include "config/AppConfig.h"
#include "llm/LLMClientFactory.h"
#include "project/ProjectManager.h"

#include <csignal>
#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>

#ifdef _WIN32
#include <windows.h>

// 将命令行参数从系统代码页转换为 UTF-8（修复 MinGW 中文乱码）。
// 仅在字符串不是有效 UTF-8 时才进行转换。
static std::string argToUtf8(const std::string& input) {
    if (input.empty()) return input;

    // 尝试将输入作为 UTF-8 解码 → 如果成功则已经是 UTF-8
    int test_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        input.c_str(), -1, nullptr, 0);
    if (test_len > 0) return input;  // 已是有效 UTF-8

    // 从系统代码页 (ACP/GBK) 转换为 UTF-8
    int wide_len = MultiByteToWideChar(CP_ACP, 0, input.c_str(), -1, nullptr, 0);
    if (wide_len <= 0) return input;

    std::wstring wide(wide_len, L'\0');
    MultiByteToWideChar(CP_ACP, 0, input.c_str(), -1, &wide[0], wide_len);

    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                                        nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 0) return input;

    std::string utf8(utf8_len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                        &utf8[0], utf8_len, nullptr, nullptr);
    if (!utf8.empty() && utf8.back() == '\0') utf8.pop_back();
    return utf8;
}
#else
static std::string argToUtf8(const std::string& input) { return input; }
#endif

// ── 全局 SIGINT 信号处理 ──
// 在 LLM 请求处理期间被触发时，设置取消标志，SSE 回调将中止 HTTP 连接。
static std::atomic<bool>* g_cancel_flag = nullptr;
extern "C" void sigint_handler(int) {
    if (g_cancel_flag) {
        g_cancel_flag->store(true);
    }
}

int main(int argc, char** argv) {
    Ansi::enableWindowsAnsi();

    CLI::App app{"NovelAgent - AI-Powered Novel Writing Assistant"};

    std::string projectPath, execCommand, providerName = "deepseek";
    bool verbose = false;

    app.add_option("-p,--project", projectPath, "项目目录路径");
    app.add_option("-e,--exec", execCommand, "执行单次命令后退出");
    app.add_option("--provider", providerName, "LLM provider (deepseek, kimi, claude)");
    app.add_flag("-v,--verbose", verbose, "启用调试日志");

    try { app.parse(argc, argv); }
    catch (const CLI::ParseError& e) { return app.exit(e); }

    // 将命令行参数从系统代码页转换为 UTF-8（修复 MinGW 中文乱码）
    projectPath = argToUtf8(projectPath);
    execCommand = argToUtf8(execCommand);

    if (verbose) spdlog::set_level(spdlog::level::debug);

    try {
        AppConfig config = AppConfig::load();
        for (const auto& name : {"DEEPSEEK", "KIMI", "CLAUDE"}) {
            if (const char* key = std::getenv((std::string(name) + "_API_KEY").c_str())) {
                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                config.setApiKey(lower, key);
            }
        }

        auto* provider = config.getProvider(providerName);
        if (!provider) {
            std::cerr << Ansi::error() << "错误: 未找到 provider '"
                      << providerName << "'\n" << Ansi::reset();
            return 1;
        }

        // 检测占位 API Key，给用户明确指引
        if (provider->api_key.empty()) {
            std::cerr << Ansi::error()
                      << "错误: 未配置 API Key。\n"
                      << Ansi::reset()
                      << Ansi::dim()
                      << "请编辑 ~/.novelagent/config.json，\n"
                      << "将 provider." << providerName << ".api_key 设为真实密钥。\n"
                      << Ansi::reset();
            return 1;
        }
        if (provider->api_key.find("请替换") != std::string::npos ||
            provider->api_key.find("your-") != std::string::npos ||
            provider->api_key.find("placeholder") != std::string::npos) {
            std::cerr << Ansi::warning()
                      << "警告: 检测到占位 API Key，请替换为真实的密钥。\n"
                      << Ansi::reset()
                      << Ansi::dim()
                      << "配置文件位置: ~/.novelagent/config.json\n"
                      << "编辑 provider." << providerName << ".api_key 字段即可。\n"
                      << Ansi::reset();
            // 不阻止启动，让用户看到错误后再修改（或继续使用环境变量覆盖）
        }

        // ── 打开项目（可选）──
        std::shared_ptr<Project> projectPtr;
        if (!projectPath.empty()) {
            ProjectManager pm;
            Project project = pm.openOrCreate(projectPath);
            if (project.title.empty()) {
                std::cerr << Ansi::error() << "错误: 无法打开/创建项目 "
                          << projectPath << "\n" << Ansi::reset();
                return 1;
            }
            projectPtr = std::make_shared<Project>(std::move(project));
        }

        NovelAgentApp novelAgent(*provider, projectPtr);

        // ── 注册 SIGINT 信号处理器 ──
        // 在 runRepl/runExec 期间，g_cancel_flag 指向 Agent 的取消标志，
        // Ctrl+C 时设置该标志，LLMClient 的 SSE 回调将中止 HTTP 连接。
        struct SigGuard {
            std::atomic<bool>* old_flag;
            SigGuard() : old_flag(g_cancel_flag) {}
            ~SigGuard() { g_cancel_flag = old_flag; }
        } sig_guard;
        g_cancel_flag = novelAgent.agent().cancelFlag();
        signal(SIGINT, sigint_handler);

        if (!execCommand.empty()) {
            novelAgent.runExec(execCommand);
            return 0;
        }

        // 默认：启动 REPL 交互模式
        novelAgent.runRepl();

    } catch (const std::exception& e) {
        std::cerr << Ansi::error() << "\n致命错误: " << e.what()
                  << Ansi::reset() << "\n";
        return 1;
    } catch (...) {
        std::cerr << Ansi::error() << "\n发生未知错误，程序将退出。\n" << Ansi::reset();
        return 1;
    }

    return 0;
}
