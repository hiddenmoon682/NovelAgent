#include "agent/AgentSetup.h"

#include "agent/ToolRegistry.h"

namespace agent {

void registerAllTools(ToolRegistry& registry,
                      const ToolDependencies& deps,
                      const std::vector<std::string>& disabled) {
    BuiltInTool::registerAllTo(registry, deps, disabled);
}

} // namespace agent
