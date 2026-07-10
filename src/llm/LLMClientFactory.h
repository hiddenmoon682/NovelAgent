#pragma once

// LLMClient 工厂 — 实例级线程隔离。
//
// 每个调用方通过 create() 获得独立的 LLMClient 实例。
// 工厂本身是线程安全的（构造后不可变），可在多线程间共享。
//
// 使用方式：
//   LLMClientFactory factory(providerConfig);
//   auto client = factory.create();  // 每次调用返回全新实例
//
// 架构意图：
//   - Agent / SubAgent 各自持有独立的 LLMClient，互不干扰
//   - AgentOrchestrator 为每个并行子任务创建独立客户端
//   - SessionManager 为每个会话创建独立 Agent（从而独立 LLMClient）

#include "config/AppConfig.h"

#include <memory>

namespace llm {

class ILLMClient;

class LLMClientFactory {
public:
    explicit LLMClientFactory(ProviderConfig config);

    // 创建新的 LLMClient 实例。
    // 每次调用返回独立实例，调用方可安全地在不同线程并发使用。
    std::unique_ptr<ILLMClient> create() const;

    // 返回当前 ProviderConfig（只读）。
    const ProviderConfig& config() const { return config_; }

private:
    ProviderConfig config_;
};

} // namespace llm
