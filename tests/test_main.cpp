// Phase 0 冒烟测试，用来确认 nlohmann/json 能正确链接并运行。
// 如果这个测试通过，说明当前构建链路和基础依赖至少是可用的。

#include <nlohmann/json.hpp>
#include <iostream>

int main() {
    nlohmann::json j;
    j["name"] = "NovelAgent";
    j["version"] = "0.1.0";

    if (j["name"] != "NovelAgent") {
        std::cerr << "JSON test FAILED\n";
        return 1;
    }

    std::cout << "All tests passed!\n";
    return 0;
}
