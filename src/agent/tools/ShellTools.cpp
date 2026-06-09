#include "agent/tools/ShellTools.h"
#include "utils/SchemaUtils.h"
#include <spdlog/spdlog.h>
#include <array>
#include <cstdio>
#include <memory>
#include <string>

namespace agent {
using json = nlohmann::json;

json RunPowerShellTool::parameters() const {
    return utils::schema::object({
        {"command", utils::schema::stringProp("要执行的 PowerShell 命令")}
    }, {"command"});
}

json RunPowerShellTool::execute(const json& args) {
    std::string cmd = args.value("command", "");
    if (cmd.empty()) return {{"error", "命令不能为空"}};

    spdlog::info("[run_powershell] {}", cmd);

    // 使用 _popen 执行命令并捕获输出
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
    }

    int exit_code = _pclose(pipe.release()); // 获取退出码

    return {
        {"stdout", output},
        {"stderr", ""},
        {"exit_code", exit_code}
    };
}

} // namespace agent
