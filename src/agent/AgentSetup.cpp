#include "agent/AgentSetup.h"

#include "agent/ToolRegistry.h"
#include "agent/tools/BuiltInTool.h"
#include "project/Models.h"

namespace agent {

void registerAllTools(ToolRegistry& registry, Project& project,
                      const std::vector<std::string>& disabled) {
    BuiltInTool::registerAllTo(registry, project, disabled);
}

} // namespace agent
