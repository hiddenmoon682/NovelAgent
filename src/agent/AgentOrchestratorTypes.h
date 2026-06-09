#pragma once

/// AgentOrchestrator 共享类型 — 拆分后各子模块共享的 DTO。
/// 从 AgentOrchestrator.h 提取，避免拆分后的类之间产生循环依赖。

#include <string>
#include <vector>

namespace agent {

/// 子任务数据模型。
struct SubTask {
    std::string id;
    std::string description;
    std::string system_prompt;
    std::vector<std::string> allowed_tools;
    std::string result;
    std::string status;   // pending|running|completed|failed|timed_out
    std::string error;
};

} // namespace agent
