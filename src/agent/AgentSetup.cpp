#include "agent/AgentSetup.h"

#include "agent/ToolRegistry.h"

#include <memory>

namespace agent {

void registerAllTools(ToolRegistry& registry,
                      std::shared_ptr<Project> project,
                      const std::vector<std::string>& disabled) {
    BuiltInTool::registerAllTo(registry, std::move(project), disabled);
}

} // namespace agent
