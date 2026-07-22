#pragma once

#include "agent/tools/BuiltInTool.h"

#include <string>
#include <vector>

namespace agent {
class ToolRegistry;

void registerAllTools(ToolRegistry& registry,
                      const ToolDependencies& deps,
                      const std::vector<std::string>& disabled = {});
} // namespace agent
