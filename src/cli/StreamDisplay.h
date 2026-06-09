#pragma once

#include "llm/ILLMClient.h"
#include <string>

/// 流式输出显示 — 将 LLM 流式回调包装为终端友好的输出。
class StreamDisplay {
public:
    /// 创建 StreamCallbacks，流式输出到 std::cout。
    static llm::StreamCallbacks create();
};
