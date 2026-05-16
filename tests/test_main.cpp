// Phase 0 smoke test — verifies nlohmann/json links and works.
// If this passes, the build toolchain and dependencies are correctly set up.

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
