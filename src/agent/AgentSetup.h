#pragma once

#include <memory>
#include <string>
#include <vector>

struct Project;

namespace agent {
class ToolRegistry;

void registerAllTools(ToolRegistry& registry,
                      std::shared_ptr<Project> project,
                      const std::vector<std::string>& disabled = {});
} // namespace agent
