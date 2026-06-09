#pragma once

/// 工具注册辅助 — 声明与实现分离，避免编译时瓶颈。
/// 声明只需前向声明，实现放在 AgentSetup.cpp 中。
/// 修改任意工具头文件不会触发 main.cpp 重新编译。

struct Project;

namespace agent {
class ToolRegistry;
void registerAllTools(ToolRegistry& registry, Project& project);
} // namespace agent
