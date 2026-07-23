#pragma once

#include "agent/context/TokenBudget.h"
#include "llm/Message.h"

#include <string>
#include <vector>

struct Project;

namespace llm {
class IMemory;
class TokenCounter;
}

namespace agent {

struct AssemblyResult {
    std::string system_prompt;
    int system_tokens = 0;
    int message_tokens = 0;
    int total_tokens = 0;
    ContextStatus status = ContextStatus::Normal;
    std::vector<std::string> warnings;
    bool fatal = false;
};

// 无状态上下文组装器。每次调用时从 Project + Memory 计算 system prompt 和 token 统计。
// 不持有 Project*、不修改 Memory、不触发 compaction。
class ContextAssembler {
public:
    AssemblyResult assemble(const Project* project,
                            const llm::IMemory& memory,
                            const TokenBudget& budget,
                            const std::string& model_name = {},
                            const llm::TokenCounter* calibrator = nullptr) const;

    static std::string buildSystemPrompt(const Project& project);
};

} // namespace agent
