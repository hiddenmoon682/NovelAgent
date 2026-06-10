// NovelAgent — AI 写小说助手。Phase 6 前后端分离版。
// 三种模式: 单体REPL | 后端Server | 单次命令
#include "NovelAgentApp.h"
#include "agent/AgentSetup.h"
#include "cli/AnsiTerminal.h"
#include "config/AppConfig.h"
#include "project/ProjectManager.h"
#include "server/BackendServer.h"

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <windows.h>

/// 将命令行参数从系统代码页转换为 UTF-8（修复 MinGW 中文乱码）。
/// 仅在字符串不是有效 UTF-8 时才进行转换。
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

// ── 查找 Node.js 安装位置 ──
static std::string findNodeExe() {
    // 1. PATH 中搜索
    if (const char* pathEnv = std::getenv("PATH")) {
        std::string pathStr(pathEnv);
        size_t start = 0;
        while (start < pathStr.size()) {
            size_t end = pathStr.find(';', start);
            if (end == std::string::npos) end = pathStr.size();
            std::string dir = pathStr.substr(start, end - start);
            if (!dir.empty() && dir.back() != '\\') dir += '\\';
            std::string candidate = dir + "node.exe";
            if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
                return candidate;
            start = end + 1;
        }
    }
    // 2. 常见安装位置
    const char* known[] = {
        "D:\\SoftWare\\NodeJs", "D:\\SoftWare\\nodejs",
        "C:\\Program Files\\nodejs", "C:\\Program Files (x86)\\nodejs",
    };
    for (auto* base : known) {
        std::string search = std::string(base) + "\\node.exe";
        if (GetFileAttributesA(search.c_str()) != INVALID_FILE_ATTRIBUTES)
            return search;
        // 也搜索一层子目录（版本化安装）
        std::string pattern = std::string(base) + "\\*";
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    if (fd.cFileName[0] != '.') {
                        std::string sub = std::string(base) + "\\" + fd.cFileName + "\\node.exe";
                        if (GetFileAttributesA(sub.c_str()) != INVALID_FILE_ATTRIBUTES) {
                            FindClose(h);
                            return sub;
                        }
                    }
                }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    return "";
}

// ── 启动 TUI 子进程 ──
static bool launchTui(const std::string& nodeExe, const std::string& projectPath,
                      int port, const std::string& tuiDir) {
    std::string tsxEntry = tuiDir + "\\src\\main.tsx";
    std::string cmdLine =
        "\"" + nodeExe + "\" " +
        "\"" + tuiDir + "\\node_modules\\tsx\\dist\\cli.mjs\" " +
        "\"" + tsxEntry + "\" " +
        "-p \"" + projectPath + "\" " +
        "--port " + std::to_string(port);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr,
                        FALSE, 0, nullptr, tuiDir.c_str(), &si, &pi)) {
        return false;
    }

    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    return true;
}
#else
static std::string argToUtf8(const std::string& input) { return input; }
#endif

int main(int argc, char** argv) {
    Ansi::enableWindowsAnsi();

    CLI::App app{"NovelAgent - AI-Powered Novel Writing Assistant"};

    std::string projectPath, backendProjectPath, execCommand, providerName = "deepseek";
    bool verbose = false, tuiMode = false;
    int wsPort = 8899;

    app.add_option("-p,--project", projectPath, "项目目录路径");
    app.add_option("-e,--exec", execCommand, "执行单次命令后退出");
    app.add_option("--provider", providerName, "LLM provider (deepseek, kimi, claude)");
    app.add_flag("-v,--verbose", verbose, "启用调试日志");
    app.add_flag("--tui", tuiMode, "启动 TUI 前端界面");

    // Phase 6: backend 子命令
    auto* backendCmd = app.add_subcommand("backend", "启动后端 HTTP+SSE 服务器");
    backendCmd->add_option("-p,--project", backendProjectPath, "项目目录路径")->required();
    backendCmd->add_option("--port", wsPort, "HTTP 端口 (默认 8899)");
    backendCmd->add_option("--provider", providerName, "LLM provider");
    backendCmd->add_flag("-v,--verbose", verbose, "启用调试日志");

    try { app.parse(argc, argv); }
    catch (const CLI::ParseError& e) { return app.exit(e); }

    // 将命令行参数从系统代码页转换为 UTF-8（修复 MinGW 中文乱码）
    projectPath = argToUtf8(projectPath);
    execCommand = argToUtf8(execCommand);
    backendProjectPath = argToUtf8(backendProjectPath);

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

        // ── backend 模式 ──
        if (app.got_subcommand("backend")) {
            ProjectManager pm;
            Project project = pm.openOrCreate(backendProjectPath);
            if (project.title.empty()) {
                std::cerr << Ansi::error() << "无法打开项目: "
                          << backendProjectPath << "\n" << Ansi::reset();
                return 1;
            }
            auto projectPtr = std::make_shared<Project>(std::move(project));

            // 创建 LLMClient + ToolRegistry
            llm::LLMClient llmClient(*provider);
            agent::ToolRegistry registry;
            agent::registerAllTools(registry, projectPtr);

            server::ServerConfig cfg;
            cfg.port = wsPort;
            cfg.project_path = backendProjectPath;
            server::BackendServer backend(llmClient, registry, projectPtr, cfg);

            std::cout << Ansi::title() << "NovelAgent Backend" << Ansi::reset()
                      << " | " << Ansi::info() << "HTTP+SSE → localhost:"
                      << wsPort << Ansi::reset() << "\n";
            std::cout << Ansi::dim() << "项目: " << projectPtr->title
                      << " | 等待前端连接...\n" << Ansi::reset();
            std::cout << Ansi::dim() << "按 Ctrl+C 停止服务器\n" << Ansi::reset();

            backend.run();
            return 0;
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

        if (!execCommand.empty()) {
            novelAgent.runExec(execCommand);
            return 0;
        }

        // ── TUI 模式：无参数双击启动时自动拉起前端 ──
        if (tuiMode || (projectPath.empty() && execCommand.empty())) {
#ifdef _WIN32
            std::string nodeExe = findNodeExe();
            if (!nodeExe.empty()) {
                // 默认项目路径
                std::string tuiProjectPath = projectPath.empty() ? ".\\my_novel" : projectPath;

                // 日志写入文件，避免干扰 TUI 终端渲染
                auto logPath = tuiProjectPath + "\\.novelagent\\backend.log";
                try {
                    auto fileLogger = spdlog::basic_logger_mt("file", logPath);
                    spdlog::set_default_logger(fileLogger);
                    spdlog::flush_on(spdlog::level::info);
                } catch (...) {}
                std::cout.setstate(std::ios::failbit);
                // TUI 目录（相对 exe 位置：build/novelagent.exe → ../tui/）
                std::string tuiDir = argToUtf8(
                    []() -> std::string {
                        char buf[MAX_PATH];
                        GetModuleFileNameA(nullptr, buf, MAX_PATH);
                        std::string exe(buf);
                        auto pos = exe.rfind('\\');
                        if (pos != std::string::npos) exe = exe.substr(0, pos);
                        pos = exe.rfind('\\');
                        if (pos != std::string::npos) exe = exe.substr(0, pos);
                        return exe + "\\tui";
                    }());

                // 确保默认项目存在
                {
                    ProjectManager pm;
                    pm.openOrCreate(tuiProjectPath);
                }

                int tuiPort = 18899;

                // 启动后端（在后台线程）
                std::atomic<bool> backendReady{false};
                std::thread backendThread([&]() {
                    try {
                        ProjectManager pm;
                        Project project = pm.openOrCreate(tuiProjectPath);
                        auto projectPtr = std::make_shared<Project>(std::move(project));
                        llm::LLMClient llmClient(*provider);
                        agent::ToolRegistry registry;
                        agent::registerAllTools(registry, projectPtr);
                        server::ServerConfig cfg;
                        cfg.port = tuiPort;
                        cfg.project_path = tuiProjectPath;
                        server::BackendServer backend(llmClient, registry, projectPtr, cfg);
                        backendReady = true;
                        backend.run();
                    } catch (const std::exception& e) {
                        spdlog::error("[TUI] 后端异常: {}", e.what());
                    }
                });

                // 等待后端就绪
                while (!backendReady) std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::this_thread::sleep_for(std::chrono::milliseconds(500));

                // 清屏后短暂恢复 cout 输出清屏码，然后再次关闭
                std::cout.clear();
                std::cout << Ansi::clearScreen() << std::flush;
                std::cout.setstate(std::ios::failbit);

                std::cout << Ansi::title() << "NovelAgent" << Ansi::reset()
                          << " | " << Ansi::dim() << "后端 localhost:" << tuiPort
                          << " | 项目: " << tuiProjectPath << Ansi::reset() << "\n";

                // 启动 TUI 前端（阻塞，等待用户退出）
                if (launchTui(nodeExe, tuiProjectPath, tuiPort, tuiDir)) {
                    std::cout << Ansi::dim() << "TUI 已退出。\n" << Ansi::reset();
                } else {
                    std::cout << Ansi::warning()
                              << "无法启动 TUI 前端，请确保已运行 npm install。\n"
                              << Ansi::reset()
                              << Ansi::dim()
                              << "cd tui && npm install\n"
                              << Ansi::reset();
                }

                // TUI 退出 → detach 后端线程 → 进程正常退出
                // BackendServer 在 lambda 栈上，进程退出时 OS 回收
                backendThread.detach();
                return 0;
            }
#endif
            // Node.js 未找到 → 回退到 REPL
            std::cout << Ansi::dim()
                      << "未找到 Node.js，使用文本界面模式。\n"
                      << "安装 Node.js 后可获得更好的视觉体验。\n"
                      << Ansi::reset() << "\n";
        }

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
