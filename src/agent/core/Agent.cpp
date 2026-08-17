// Agent 实现 — 多会话并行门面（D2 收敛后）。
//
// 全部会话能力转发给 SessionPool（会话池容器）；本文件仅保留构造/析构。

#include "agent/core/Agent.h"
#include "agent/session/SessionPool.h"

namespace agent {

Agent::Agent(llm::LLMClientFactory& factory, ToolRegistry& registry)
    : factory_(factory)
    , registry_(registry)
    , session_pool_(factory, registry)
{
}

Agent::~Agent() = default;

} // namespace agent