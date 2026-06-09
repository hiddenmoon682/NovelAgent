#include "cli/StreamDisplay.h"
#include <iostream>

llm::StreamCallbacks StreamDisplay::create() {
    llm::StreamCallbacks cb;

    cb.on_content = [](const std::string& delta) {
        std::cout << delta << std::flush;
    };

    cb.on_reasoning = [](const std::string& delta) {
        // 思维链以暗色显示
        std::cout << "\033[90m" << delta << "\033[0m" << std::flush;
    };

    cb.on_tool_call_start = []() {
        std::cout << "\n  \033[90m[工具调用...]\033[0m " << std::flush;
    };

    cb.on_complete = [](const llm::LLMResponse& resp) {
        if (resp.total_tokens > 0) {
            std::cout << "\n  \033[90m(" << resp.total_tokens << " tokens)\033[0m";
        }
    };

    cb.on_error = [](const std::string& err) {
        std::cerr << "\n  \033[31m错误: " << err << "\033[0m\n";
    };

    return cb;
}
