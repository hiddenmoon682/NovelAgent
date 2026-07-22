#include "agent/tools/BuiltInTool.h"
#include "agent/ToolRegistry.h"
#include <algorithm>

namespace agent {

std::vector<BuiltInTool::Entry>& BuiltInTool::factories() {
    static std::vector<Entry> f;
    return f;
}

void BuiltInTool::registerFactory(std::string name, Factory factory) {
    factories().push_back({std::move(name), std::move(factory)});
}

void BuiltInTool::registerAllTo(ToolRegistry& registry,
                                 const ToolDependencies& deps,
                                 const std::vector<std::string>& disabled) {
    for (auto& entry : factories()) {
        if (std::find(disabled.begin(), disabled.end(), entry.name) != disabled.end())
            continue;
        registry.registerBuiltInTool(entry.factory(deps));
    }
}

const std::vector<std::string>& BuiltInTool::registeredToolNames() {
    static std::vector<std::string> names;
    if (names.empty()) {
        for (auto& e : factories()) names.push_back(e.name);
    }
    return names;
}

} // namespace agent
