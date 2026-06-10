#include "agent/tools/ShellTools.h"
#include "utils/SchemaUtils.h"
#include <spdlog/spdlog.h>
#include <string>
#include <set>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

namespace agent {
using json = nlohmann::json;

json RunPowerShellTool::parameters() const {
    return utils::schema::object({
        {"command", utils::schema::stringProp("要执行的 PowerShell 命令")}
    }, {"command"});
}

namespace {

bool isDangerousCommand(const std::string& cmd) {
    static const std::set<std::string> blocked = {
        "remove-item", "ri ", "rm ", "del ", "rmdir", "rd ", "rdr ",
        "erase", "delete", "clear-content", "clear-item",
        "format", "diskpart", "initialize-disk", "clear-disk",
        "stop-process", "kill", "spps ", "stop-service", "sasv ",
        "set-content", "set-itemproperty", "set-service",
        "out-file", "out-printer",
        "invoke-webrequest", "iwr ", "invoke-restmethod", "irm ",
        "curl", "wget", "start-process", "saps ",
        "iex ", "invoke-expression", "invoke-command", "icm ",
        "new-object", "download", "upload",
        "net user", "net localgroup", "net group",
        "new-localuser", "add-localgroupmember",
        "reg add", "reg delete", "reg import",
        "shutdown", "restart-computer", "stop-computer", "logoff",
    };
    std::string lower = cmd;
    for (char& c : lower) c = static_cast<char>(std::tolower(c));
    for (const auto& kw : blocked)
        if (lower.find(kw) != std::string::npos) return true;
    return false;
}

/// 使用 CreateProcess 执行命令，支持超时（Windows）。
/// @param cmd     PowerShell 命令
/// @param output  出参：stdout 输出
/// @param timeoutMs 超时毫秒数
/// @return        进程退出码（超时时返回 -1）
int execWithTimeout(const std::string& cmd, std::string& output, DWORD timeoutMs = 30000) {
#ifdef _WIN32
    std::string fullCmd = "powershell.exe -NoProfile -Command " + cmd;

    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return -1;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {sizeof(STARTUPINFOA)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(nullptr, const_cast<char*>(fullCmd.c_str()),
                        nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        CloseHandle(hRead); CloseHandle(hWrite);
        return -1;
    }
    CloseHandle(hWrite);

    // 等待进程完成或超时
    DWORD waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);

    int exitCode = -1;
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        output = "(命令超时)";
    } else {
        // 读取输出
        char buf[256];
        DWORD read;
        while (ReadFile(hRead, buf, sizeof(buf) - 1, &read, nullptr) && read > 0) {
            buf[read] = '\0';
            output += buf;
            if (output.size() > 100 * 1024) {
                output += "\n...(已截断)";
                break;
            }
        }
        GetExitCodeProcess(pi.hProcess, reinterpret_cast<LPDWORD>(&exitCode));
    }

    CloseHandle(hRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exitCode;
#else
    // 非 Windows：回退到 _popen
    std::string fullCmd = "powershell -NoProfile -Command " + cmd;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(fullCmd.c_str(), "r"), pclose);
    if (!pipe) return -1;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe.get())) {
        output += buf;
        if (output.size() > 100 * 1024) { output += "\n...(已截断)"; break; }
    }
    return 0;
#endif
}

} // namespace

json RunPowerShellTool::execute(const json& args) {
    std::string cmd = args.value("command", "");
    if (cmd.empty()) return {{"error", "命令不能为空"}};

    if (isDangerousCommand(cmd)) {
        spdlog::warn("[run_powershell] 拦截危险命令: {}", cmd);
        return {{"error", "命令被安全策略拦截"}, {"blocked", true}};
    }

    spdlog::info("[run_powershell] {}", cmd);

    std::string output;
    int exitCode = execWithTimeout(cmd, output, 30000); // 30s timeout

    return {
        {"stdout", output},
        {"stderr", ""},
        {"exit_code", exitCode}
    };
}

} // namespace agent

// ShellTools 不接收 Project& 参数，手动注册
namespace {
    static const bool _reg_ShellTools = []() {
        agent::BuiltInTool::registerFactory("run_powershell",
            [](std::shared_ptr<Project>) -> std::unique_ptr<agent::BuiltInTool> {
                return std::make_unique<agent::RunPowerShellTool>();
            });
        return true;
    }();
}
