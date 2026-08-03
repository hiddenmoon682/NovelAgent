#include "agent/tools/BuiltInTool.h"
#include "agent/tool/ToolRegistry.h"

namespace agent {

std::vector<BuiltInTool::Entry>& BuiltInTool::factories() {
    static std::vector<Entry> f;
    return f;
}

void BuiltInTool::registerFactory(std::string name, Factory factory) {
    factories().push_back({std::move(name), std::move(factory)});
}

// 将全部已注册的内置工具工厂实例化并注册到传入的注册表。
// 由 App 装配阶段（setupAgent）调用一次；每个工具工厂用共享依赖 deps 构造，
// 使其能访问项目/向量库/记忆/技能等资源。
void BuiltInTool::registerAllTo(ToolRegistry& registry,
                                 const ToolDependencies& deps) {
    // 遍历静态工厂表（REGISTER_TOOL 宏在静态初始化时填充），逐个实例化并注册
    for (auto& entry : factories())
        registry.registerBuiltInTool(entry.factory(deps));
}

const std::vector<std::string>& BuiltInTool::registeredToolNames() {
    static std::vector<std::string> names;
    if (names.empty()) {
        for (auto& e : factories()) names.push_back(e.name);
    }
    return names;
}

} // namespace agent
