#include "agent/AgentSetup.h"

#include "agent/ToolRegistry.h"
#include "agent/tools/ChapterTools.h"
#include "agent/tools/CharacterTools.h"
#include "agent/tools/SettingTools.h"
#include "agent/tools/WorldRuleTools.h"
#include "agent/tools/OutlineTools.h"
#include "agent/tools/ShellTools.h"
#include "project/Models.h"

#include <memory>

namespace agent {

void registerAllTools(ToolRegistry& registry, Project& project) {
    // Chapter 工具
    registry.registerBuiltInTool(std::make_unique<ReadChapterTool>(project));
    registry.registerBuiltInTool(std::make_unique<WriteChapterTool>(project));
    registry.registerBuiltInTool(std::make_unique<AppendChapterTool>(project));
    registry.registerBuiltInTool(std::make_unique<ListChaptersTool>(project));
    registry.registerBuiltInTool(std::make_unique<CreateChapterTool>(project));

    // Character 工具
    registry.registerBuiltInTool(std::make_unique<GetCharacterTool>(project));
    registry.registerBuiltInTool(std::make_unique<ListCharactersTool>(project));
    registry.registerBuiltInTool(std::make_unique<CreateCharacterTool>(project));
    registry.registerBuiltInTool(std::make_unique<UpdateCharacterTool>(project));

    // Setting 工具
    registry.registerBuiltInTool(std::make_unique<GetSettingTool>(project));
    registry.registerBuiltInTool(std::make_unique<ListSettingsTool>(project));
    registry.registerBuiltInTool(std::make_unique<UpdateSettingTool>(project));

    // WorldRule 工具
    registry.registerBuiltInTool(std::make_unique<GetWorldRuleTool>(project));
    registry.registerBuiltInTool(std::make_unique<ListWorldRulesTool>(project));
    registry.registerBuiltInTool(std::make_unique<UpdateWorldRuleTool>(project));

    // Outline + Project 工具
    registry.registerBuiltInTool(std::make_unique<GetOutlineTool>(project));
    registry.registerBuiltInTool(std::make_unique<GetProjectStatusTool>(project));

    // Shell 工具
    registry.registerBuiltInTool(std::make_unique<RunPowerShellTool>());
}

} // namespace agent
