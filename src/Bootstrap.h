#pragma once

// ============================================================================
// Bootstrap — CLI 和 GUI 入口共享的启动逻辑。
//
// 职责：
//   1. 解析命令行参数（CLI11）
//   2. 加载并校验 AppConfig 配置（环境变量覆盖）
//   3. 通过 ProjectManager 打开或创建项目
//   4. 构造 NovelAgentApp 实例
//   5. 注册 SIGINT（Ctrl+C）信号处理，支持优雅退出
//
// 使用方式：
//   auto ctx = bootstrap::run(argc, argv);
//   if (ctx.exitCode != -1) return ctx.exitCode;
//   // 根据 ctx.cliMode 选择 REPL 或 GUI 模式
//
// 注意：所有函数均为 inline，无需对应的 .cpp 文件。
// ============================================================================

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

// 将命令行参数从系统本地编码（Windows CP_ACP）转换为 UTF-8。
// Windows 控制台使用本地代码页（如 GBK），而 NovelAgent 内部统一使用 UTF-8。
// 此函数在入口处做编码转换，确保后续处理不会出现乱码。
//
// @param input  原始参数字符串（可能是 CP_ACP 或 UTF-8）
// @return       转换后的 UTF-8 字符串
//
// 注意：非 Windows 平台直接返回原值（argv 通常已是 UTF-8）。
// 注意：如果输入已经是合法的 UTF-8，则跳过转换以避免双重编码。
inline std::string argToUtf8(const std::string& input) {
#ifdef _WIN32
    if (input.empty()) return input;

    // 步骤 1: 检测是否已经是合法的 UTF-8
    // 用 MB_ERR_INVALID_CHARS 标志检测，成功则跳过转换
    int test_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        input.c_str(), -1, nullptr, 0);
    if (test_len > 0) return input;

    // 步骤 2: 将本地编码（CP_ACP）转换为宽字符（UTF-16）
    int wide_len = MultiByteToWideChar(CP_ACP, 0, input.c_str(), -1, nullptr, 0);
    if (wide_len <= 0) return input;
    std::wstring wide(wide_len, L'\0');
    MultiByteToWideChar(CP_ACP, 0, input.c_str(), -1, &wide[0], wide_len);

    // 步骤 3: 将宽字符转换为 UTF-8
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                                        nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 0) return input;
    std::string utf8(utf8_len, L'\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                        &utf8[0], utf8_len, nullptr, nullptr);

    // 去除尾部的空字符（WideCharToMultiByte 输出的末尾 \0）
    if (!utf8.empty() && utf8.back() == L'\0') utf8.pop_back();
    return utf8;
#else
    // 非 Windows 平台：argv 通常已经是 UTF-8，直接返回
    return input;
#endif
}

// ============================================================================
// 全局取消标志与 SIGINT 处理
// ============================================================================

// 原子取消标志指针，用于跨线程通知 Agent 停止。
// run() 函数在构造 NovelAgentApp 后，将其内部的取消标志地址
// 赋值给此全局变量，供 sigint_handler 在信号上下文中安全写入。
inline std::atomic<bool>* g_cancel_flag = nullptr;

// SIGINT（Ctrl+C）信号处理函数。
// 不直接调用 exit()，而是设置取消标志让 Agent 主循环检查后自行清理退出。
// 这样可以避免强制中断导致的数据损坏或资源泄漏。
// 注意：信号处理函数应尽量简短，只做原子写入操作。
extern "C" inline void sigint_handler(int) {
    if (g_cancel_flag) g_cancel_flag->store(true);
}

// ============================================================================
// Context — 启动结果，供调用方决定后续流程
// ============================================================================

// bootstrap::run() 的返回结果。
// 调用方根据此结构体决定后续流程：
//   - exitCode != -1 → 直接退出（错误或帮助信息已打印）
//   - cliMode == true → 进入终端 REPL 循环
//   - cliMode == false → 启动 QML GUI（未来）
struct Context {
    std::unique_ptr<NovelAgentApp> app;
    std::string execCommand;
    bool cliMode = false;
    int exitCode = -1;
};

inline Context run(int argc, char** argv) {
    // ---- 阶段 0: 前置初始化 ----
    // 启用 Windows 终端 ANSI 转义序列支持（彩色输出）
    Ansi::enableWindowsAnsi();
    Context ctx;

    // ---- 阶段 1: 命令行参数解析 ----
    CLI::App cli{"NovelAgent - AI-Powered Novel Writing Assistant"};
    std::string projectPath, providerName = "deepseek";
    bool verbose = false;

    // 项目路径：指定已有项目目录或新项目创建位置
    cli.add_option("-p,--project", projectPath, "项目目录路径");
    // 单次执行模式：执行一条命令后直接退出（用于脚本调用）
    cli.add_option("-e,--exec", ctx.execCommand, "执行单次命令后退出");
    // LLM 提供商选择：与 config.json 中的 provider 名称对应
    cli.add_option("--provider", providerName, "LLM provider (deepseek, kimi, claude)");
    cli.add_flag("-v,--verbose", verbose, "启用调试日志");
    // --cli 标志：强制使用终端交互模式，不检测 QML 是否可用
    cli.add_flag("--cli", ctx.cliMode, "强制使用终端 REPL 模式（不启动 QML GUI）");

    try { cli.parse(argc, argv); }
    catch (const CLI::ParseError& e) { ctx.exitCode = cli.exit(e); return ctx; }

    // ---- 阶段 2: 编码转换 ----
    // Windows 下 argv 可能是本地编码（如 GBK），统一转为 UTF-8
    projectPath = argToUtf8(projectPath);
    ctx.execCommand = argToUtf8(ctx.execCommand);

    // ---- 阶段 3: 日志级别设置 ----
    if (verbose) spdlog::set_level(spdlog::level::debug);

    // ---- 阶段 4: 配置加载与 Provider 校验 ----
    try {
        AppConfig config = AppConfig::load();

        // 环境变量优先：从 DEEPSEEK_API_KEY / KIMI_API_KEY / CLAUDE_API_KEY
        // 读取密钥并覆盖配置文件中的值，方便 CI/CD 和容器化部署
        for (const auto& name : {"DEEPSEEK", "KIMI", "CLAUDE"}) {
            if (const char* key = std::getenv((std::string(name) + "_API_KEY").c_str())) {
                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                config.setApiKey(lower, key);
            }
        }

        // 校验 1: 检查 provider 是否存在
        auto* provider = config.getProvider(providerName);
        if (!provider) {
            std::cerr << Ansi::error() << "错误: 未找到 provider '"
                      << providerName << "'\n" << Ansi::reset();
            ctx.exitCode = 1;
            return ctx;
        }

        // 校验 2: 检查 API Key 是否为空
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

        // 校验 3: 检测占位符 API Key，仅警告不阻止运行
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

        // ---- 阶段 5: 项目打开/创建 ----
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
            // 将 Project 提升为 shared_ptr，供 NovelAgentApp 和工具共享所有权
            projectPtr = std::make_shared<Project>(std::move(project));
        }
        // 若未指定项目路径，projectPtr 保持 nullptr，
        // 应用启动后处于"无项目"状态，等待用户后续创建或打开

        // ---- 阶段 6: 构造 NovelAgentApp ----
        ctx.app = std::make_unique<NovelAgentApp>(*provider, projectPtr);

        // ---- 阶段 7: 注册 SIGINT 信号处理 ----
        // 从 Agent 获取原子取消标志指针，赋值给全局变量
        // 这样 sigint_handler 可以在信号上下文中安全地通知 Agent 停止
        g_cancel_flag = ctx.app->agent().cancelFlag();
        signal(SIGINT, sigint_handler);

    } catch (const std::exception& e) {
        // 捕获所有标准异常，打印友好错误信息
        std::cerr << Ansi::error() << "\n致命错误: " << e.what()
                  << Ansi::reset() << "\n";
        ctx.exitCode = 1;
    } catch (...) {
        // 捕获非标准异常（如 Windows SEH），兜底处理
        std::cerr << Ansi::error() << "\n发生未知错误，程序将退出。\n"
                  << Ansi::reset();
        ctx.exitCode = 1;
    }

    return ctx;
}

} // namespace bootstrap
