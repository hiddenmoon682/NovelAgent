#pragma once

// REPL（读入-执行-输出循环）处理器。
// Phase 0 仅提供占位实现。
// Phase 3 会扩展为完整交互循环，支持历史记录、斜杠命令和流式输出。
//   - 用户输入通过 std::getline 读取，保持简单且可移植。
//   - /help、/save、/model 等斜杠命令会在本地拦截处理。
//   - 其余输入再交给 Agent 做 LLM 处理。

#include <string>

class ReplHandler {
public:
    explicit ReplHandler() = default;
    void run();
};
