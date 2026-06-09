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

/// 危险命令关键词黑名单。
/// 仅拦截破坏性操作（删除/格式化/修改系统/下载执行/提权等），
/// 允许正常的管道和重定向操作（| ; && || > >> <）。
/// 注意：黑名单不能提供绝对安全，不可在完全不可信环境中运行。
bool isDangerousCommand(const std::string& cmd) {
    // 破坏性 cmdlet（含常见 PowerShell 缩写/别名）
    static const std::set<std::string> blocked = {
        // 删除/清理操作
        "remove-item", "ri ", "rm ", "del ", "rmdir", "rd ", "rdr ",
        "erase", "delete", "clear-content", "clear-item",
        // 格式化/磁盘操作
        "format", "diskpart", "initialize-disk", "clear-disk",
        // 进程/服务操作
        "stop-process", "kill", "spps ", "stop-service", "sasv ",
        // 系统配置修改
        "set-content", "set-itemproperty", "set-service",
        "out-file", "out-printer",
        // 网络下载/执行（远程代码执行风险）
        "invoke-webrequest", "iwr ", "invoke-restmethod", "irm ",
        "curl", "wget", "start-process", "saps ",
        "iex ", "invoke-expression", "invoke-command", "icm ",
        "new-object", "download", "upload",
        // 账户/权限操作
        "net user", "net localgroup", "net group",
        "new-localuser", "add-localgroupmember",
        // 注册表修改
        "reg add", "reg delete", "reg import",
        "set-itemproperty -path registry",
        // 系统关机/重启
        "shutdown", "restart-computer", "stop-computer",
        "logoff",
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

    // 使用 _popen 执行命令并捕获输出。
    // 安全依赖 isDangerousCommand() 黑名单（上面），不是绝对安全。
    // TODO: Phase 3.5 迁移到 CreateProcess + WaitForSingleObject 实现超时控制。
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
