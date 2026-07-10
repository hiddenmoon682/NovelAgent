#pragma once

// AgentOrchestrator 共享类型 — 拆分后各子模块共享的 DTO。
// 从 AgentOrchestrator.h 提取，避免拆分后的类之间产生循环依赖。

#include <string>
#include <vector>

namespace agent {

// 子任务数据模型。
struct SubTask {
    std::string id;
    std::string description;
    std::string system_prompt;
    std::vector<std::string> allowed_tools;
    std::string result;
    std::string status;   // pending|running|completed|failed|timed_out
    std::string error;
    int suggested_max_rounds = 3;  //  A18: 从模板差分配，控制子Agent的多轮tool_call上限
    int timeout_seconds = 120;     //  MED-2: 子任务超时秒数，从模板差分配或默认 120s
};

} // namespace agent
