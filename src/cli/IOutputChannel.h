#pragma once

#include <string>

/// 输出通道抽象 — 解耦 CLI 层与具体的输出目标。
///
/// 实现:
///   ConsoleOutput — 当前 std::cout/stderr 行为
///   StringOutput   — 测试时捕获输出
///   (未来) WebSocketOutput — Web 前端

class IOutputChannel {
public:
    virtual ~IOutputChannel() = default;

    virtual void write(const std::string& text) = 0;
    virtual void writeError(const std::string& text) = 0;
};
