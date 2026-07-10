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

// C1/C2 修复：白名单替代黑名单，仅允许只读查询 cmdlet。
// 安全声明：此白名单不提供绝对安全（LLM 仍可能通过管道组合绕过），
// 它只是纵深防御的一层。Shell 工具存在的主要价值是为高级用户提供
// 文件列表/文本搜索等便捷查询能力。
static bool isAllowedCommand(const std::string& cmd) {
    // 拦截脚本注入字符（在任何 token 提取之前检测）
    // $( / ` : 子命令替换；{ } : 脚本块（可放任意表达式）；; & : 命令分隔/后台执行
    // 这些都是 PowerShell 逃逸白名单逐段校验的向量，必须在入口拦截。
    if (cmd.find("$(") != std::string::npos ||
        cmd.find("`") != std::string::npos ||
        cmd.find("{") != std::string::npos ||
        cmd.find("}") != std::string::npos ||
        cmd.find(";") != std::string::npos ||
        cmd.find("&") != std::string::npos ||
        (cmd.find(". ") != std::string::npos && cmd.find(".\\") == std::string::npos))
        return false;

    // 仅允许只读查询 cmdlet 和常见别名。
    // 注意：不放 ForEach-Object —— 它接受脚本块 { ... }，可执行任意表达式，
    // 而脚本块已被上方拦截，故即使 LLM 尝试也会被拒。Select-Object/Where-Object
    // 的脚本块同理，但它们对"只读查询"用途更基础，保留并依赖脚本块拦截兜底。
    static const std::set<std::string> allowed = {
        "get-childitem", "get-content", "get-item", "test-path",
        "select-string", "get-location", "get-date", "get-filehash",
        "measure-object", "select-object", "where-object",
        "sort-object", "group-object",
        "get-command", "get-help", "get-process", "get-service",
        "get-itemproperty", "get-acl", "get-member",
        "write-output", "write-host",
        "ls", "dir", "cat", "type", "gi", "pwd", "sls", "sort", "group",
        "echo", "ps", "gp", "gl",
    };
    // 提取管道中每段的首个 token（cmdlet 名或别名），逐段校验。
    // 关键：只校验每段的"首个 token"（命令名），段内参数（如 Get-Content 的文件名）
    // 不在白名单也不应被拦——否则任何带参数的命令都会被误判为非法。
    std::string lower = cmd;
    for (char& c : lower) c = static_cast<char>(std::tolower(c));

    size_t pos = 0;
    bool any_token = false;
    while (pos < lower.size()) {
        // 跳过段首空白
        while (pos < lower.size() && lower[pos] == ' ') ++pos;
        if (pos >= lower.size()) break;

        // 提取本段首个 token（直到空格或管道符）
        std::string token;
        while (pos < lower.size() && lower[pos] != ' ' && lower[pos] != '|') {
            token += lower[pos]; ++pos;
        }
        if (!token.empty()) {
            any_token = true;
            if (allowed.find(token) == allowed.end())
                return false;  // 段首 cmdlet 不在白名单 → 拒绝
        }

        // 跳过本段剩余参数（直到下一个管道符 | 或串尾），不校验参数 token
        while (pos < lower.size() && lower[pos] != '|') ++pos;
        // 跳过管道符本身
        if (pos < lower.size() && lower[pos] == '|') ++pos;
    }
    return any_token;  // 至少有一个合法 cmdlet 才放行（拒绝纯空白输入）
}

// 使用 CreateProcess 执行命令，支持超时（Windows）。
// @param cmd     PowerShell 命令（已通过白名单校验的安全查询命令）
// @param output  出参：stdout 输出
// @param timeoutMs 超时毫秒数
// @return        进程退出码（超时时返回 -1）
int execWithTimeout(const std::string& cmd, std::string& output, DWORD timeoutMs = 30000) {
#ifdef _WIN32
    // PowerShell 命令用双引号包裹，确保含空格的文件路径等参数正确传递
    // 注意：cmd 本身经过白名单校验 + 注入字符过滤，不含 $() ` {} ; & 等注入向量
    std::string fullCmd = "powershell.exe -NoProfile -Command \"" + cmd + "\"";

    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return -1;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(STARTUPINFOA);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi = {};
    // CreateProcessA 的 lpCommandLine 参数可能修改缓冲区内容（替换空格为 \0 以解析 argv）。
    // 因此需要可写缓冲区，不能直接传 c_str() 的 const 指针。
    std::vector<char> mutableCmd(fullCmd.begin(), fullCmd.end() + 1); // +1 包含结尾 \0
    if (!CreateProcessA(nullptr, mutableCmd.data(),
                        nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        CloseHandle(hRead); CloseHandle(hWrite);
        return -1;
    }
    CloseHandle(hWrite);

    // 等待进程完成或超时
    DWORD waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);

    DWORD exitCode = (DWORD)-1;
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
        GetExitCodeProcess(pi.hProcess, &exitCode);
    }

    CloseHandle(hRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
#else
    (void)cmd; (void)output; (void)timeoutMs;
    output = "(Shell 工具在非 Windows 平台不可用)";
    return -1;
#endif
}

} // namespace

json RunPowerShellTool::execute(const json& args) {
    std::string cmd = args.value("command", "");
    if (cmd.empty()) return {{"error", "命令不能为空"}};

    if (!isAllowedCommand(cmd)) {
        spdlog::warn("[run_powershell] 拦截非白名单命令: {}", cmd);
        return {{"error", "命令不在允许的白名单中（仅允许 Get-ChildItem/Get-Content/Select-String 等只读查询）"}, {"blocked", true}};
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
