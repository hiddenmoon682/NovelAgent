#pragma once

// Bootstrap — CLI 和 GUI 入口共享的启动逻辑。
// 参数解析、配置加载、项目打开、NovelAgentApp 构造、SIGINT 注册。

#include "NovelAgentApp.h"
#include "cli/AnsiTerminal.h"
#include "config/AppConfig.h"
#include "project/ProjectManager.h"

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace bootstrap {

inline std::string argToUtf8(const std::string& input) {
#ifdef _WIN32
    if (input.empty()) return input;
    int test_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        input.c_str(), -1, nullptr, 0);
    if (test_len > 0) return input;
    int wide_len = MultiByteToWideChar(CP_ACP, 0, input.c_str(), -1, nullptr, 0);
    if (wide_len <= 0) return input;
    std::wstring wide(wide_len, L'\0');
    MultiByteToWideChar(CP_ACP, 0, input.c_str(), -1, &wide[0], wide_len);
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                                        nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 0) return input;
    std::string utf8(utf8_len, L'\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                        &utf8[0], utf8_len, nullptr, nullptr);
    if (!utf8.empty() && utf8.back() == L'\0') utf8.pop_back();
    return utf8;
#else
    return input;
#endif
}

inline std::atomic<bool>* g_cancel_flag = nullptr;

extern "C" inline void sigint_handler(int) {
    if (g_cancel_flag) g_cancel_flag->store(true);
}

struct Context {
    std::unique_ptr<NovelAgentApp> app;
    std::string execCommand;
    bool cliMode = false;
    int exitCode = -1;
};

inline Context run(int argc, char** argv) {
    Ansi::enableWindowsAnsi();
    Context ctx;

    CLI::App cli{"NovelAgent - AI-Powered Novel Writing Assistant"};
    std::string projectPath, providerName = "deepseek";
    bool verbose = false;

    cli.add_option("-p,--project", projectPath, "项目目录路径");
    cli.add_option("-e,--exec", ctx.execCommand, "执行单次命令后退出");
    cli.add_option("--provider", providerName, "LLM provider (deepseek, kimi, claude)");
    cli.add_flag("-v,--verbose", verbose, "启用调试日志");
    cli.add_flag("--cli", ctx.cliMode, "强制使用终端 REPL 模式（不启动 QML GUI）");

    try { cli.parse(argc, argv); }
    catch (const CLI::ParseError& e) { ctx.exitCode = cli.exit(e); return ctx; }

    projectPath = argToUtf8(projectPath);
    ctx.execCommand = argToUtf8(ctx.execCommand);

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
            ctx.exitCode = 1;
            return ctx;
        }

        if (provider->api_key.empty()) {
            std::cerr << Ansi::error()
                      << "错误: 未配置 API Key。\n" << Ansi::reset()
                      << Ansi::dim()
                      << "请编辑 ~/.novelagent/config.json，\n"
                      << "将 provider." << providerName << ".api_key 设为真实密钥。\n"
                      << Ansi::reset();
            ctx.exitCode = 1;
            return ctx;
        }
        if (provider->api_key.find("请替换") != std::string::npos ||
            provider->api_key.find("your-") != std::string::npos ||
            provider->api_key.find("placeholder") != std::string::npos) {
            std::cerr << Ansi::warning()
                      << "警告: 检测到占位 API Key，请替换为真实的密钥。\n" << Ansi::reset()
                      << Ansi::dim()
                      << "配置文件位置: ~/.novelagent/config.json\n"
                      << "编辑 provider." << providerName << ".api_key 字段即可。\n"
                      << Ansi::reset();
        }

        std::shared_ptr<Project> projectPtr;
        if (!projectPath.empty()) {
            ProjectManager pm;
            Project project = pm.openOrCreate(projectPath);
            if (project.title.empty()) {
                std::cerr << Ansi::error() << "错误: 无法打开/创建项目 "
                          << projectPath << "\n" << Ansi::reset();
                ctx.exitCode = 1;
                return ctx;
            }
            projectPtr = std::make_shared<Project>(std::move(project));
        }

        ctx.app = std::make_unique<NovelAgentApp>(*provider, projectPtr);

        g_cancel_flag = ctx.app->agent().cancelFlag();
        signal(SIGINT, sigint_handler);

    } catch (const std::exception& e) {
        std::cerr << Ansi::error() << "\n致命错误: " << e.what()
                  << Ansi::reset() << "\n";
        ctx.exitCode = 1;
    } catch (...) {
        std::cerr << Ansi::error() << "\n发生未知错误，程序将退出。\n"
                  << Ansi::reset();
        ctx.exitCode = 1;
    }

    return ctx;
}

} // namespace bootstrap
