#pragma once

#include "cli/IOutputChannel.h"
#include <iostream>

/// 标准控制台输出实现。
class ConsoleOutput : public IOutputChannel {
public:
    void write(const std::string& text) override {
        std::cout << text << std::flush;
    }
    void writeError(const std::string& text) override {
        std::cerr << text << std::flush;
    }
};
