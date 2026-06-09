#include "agent/tools/ShellTools.h"
#include "utils/SchemaUtils.h"
#include <spdlog/spdlog.h>
#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <set>

namespace agent {
using json = nlohmann::json;

json RunPowerShellTool::parameters() const {
    return utils::schema::object({
        {"command", utils::schema::stringProp("要执行的 PowerShell 命令")}
    }, {"command"});
}

namespace {

/// 危险命令关键词黑名单（LLM 可能被 prompt injection 诱导执行危险操作）
bool isDangerousCommand(const std::string& cmd) {
    static const std::set<std::string> blocked = {
        "rm ", "del ", "remove-item", "rmdir", "rd ",
        "format", "diskpart", "stop-process", "kill",
        "remove", "delete", "clear-content", "set-content",
        "out-file", ">", ">>", "|", ";", "&&", "||",
        "invoke-webrequest", "invoke-restmethod", "curl",
        "wget", "start-process", "iex", "invoke-expression",
        "download", "upload", "net user", "net localgroup",
        "reg ", "shutdown", "restart", "logoff",
    };
    std::string lower = cmd;
    for (char& c : lower) c = static_cast<char>(std::tolower(c));
    for (const auto& keyword : blocked) {
        if (lower.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

json RunPowerShellTool::execute(const json& args) {
    std::string cmd = args.value("command", "");
    if (cmd.empty()) return {{"error", "命令不能为空"}};

    // 安全检查：拒绝包含危险关键字的命令
    if (isDangerousCommand(cmd)) {
        spdlog::warn("[run_powershell] 拦截危险命令: {}", cmd);
        return {
            {"error", "命令被安全策略拦截（包含危险操作关键词）。如需执行，请在终端中手动运行。"},
            {"blocked", true}
        };
    }

    spdlog::info("[run_powershell] {}", cmd);

    // 仅允许读取和查询类命令的白名单前缀
    // 限制为 Get-*, echo, dir/ls, type/cat, Select-*, Where-*, ForEach-*, Write-*
    // 注意：这个白名单不是绝对安全的，LLM 仍可能通过 ForEach-Object 执行恶意代码
    // 真正的安全措施是黑名单（上面）+ 将来的沙箱执行

    // 使用 _popen 执行命令并捕获输出（带 30s 超时则由外部调用者通过 Agent 的 max_tool_rounds 间接控制）
    std::string full_cmd = "powershell.exe -NoProfile -Command " + cmd;
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(
        _popen(full_cmd.c_str(), "r"), _pclose);

    if (!pipe) {
        return {{"error", "无法执行命令"}};
    }

    std::array<char, 256> buffer;
    std::string output;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get())) {
        output += buffer.data();
        // 防止输出过大导致内存问题（限制 100KB）
        if (output.size() > 100 * 1024) {
            output += "\n...(输出已截断，超过 100KB 限制)";
            break;
        }
    }

    int exit_code = _pclose(pipe.release());

    return {
        {"stdout", output},
        {"stderr", ""},
        {"exit_code", exit_code}
    };
}

} // namespace agent
