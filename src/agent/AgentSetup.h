#pragma once

#include <string>
#include <vector>

struct Project;

namespace agent {
class ToolRegistry;

/// 将所有已注册工具实例化到 ToolRegistry。
/// @param disabled  禁用的工具名列表（空=全部启用）
void registerAllTools(ToolRegistry& registry, Project& project,
                      const std::vector<std::string>& disabled = {});
} // namespace agent
