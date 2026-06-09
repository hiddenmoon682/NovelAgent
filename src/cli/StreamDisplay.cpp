#include "cli/StreamDisplay.h"

#ifdef _WIN32
#include <windows.h>
namespace {
    bool g_ansiEnabled = []() -> bool {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut == INVALID_HANDLE_VALUE) return false;
        DWORD mode = 0;
        if (!GetConsoleMode(hOut, &mode)) return false;
        return SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
    }();
}
#endif

llm::StreamCallbacks StreamDisplay::create(IOutputChannel& out) {
    llm::StreamCallbacks cb;

    cb.on_content = [&out](const std::string& delta) {
        out.write(delta);
    };

    cb.on_reasoning = [&out](const std::string& delta) {
        out.write("\033[90m" + delta + "\033[0m");
    };

    cb.on_tool_call_start = [&out]() {
        out.write("\n  \033[90m[工具调用...]\033[0m ");
    };

    cb.on_complete = [&out](const llm::LLMResponse& resp) {
        if (resp.total_tokens > 0) {
            out.write("\n  \033[90m(" + std::to_string(resp.total_tokens) + " tokens)\033[0m");
        }
    };

    cb.on_error = [&out](const std::string& err) {
        out.writeError("\n  \033[31m错误: " + err + "\033[0m\n");
    };

    return cb;
}
