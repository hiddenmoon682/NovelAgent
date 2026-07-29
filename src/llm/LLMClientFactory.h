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
//   - Agent 持有独立的 LLMClient，确保线程隔离

#include "config/AppConfig.h"

#include <memory>

namespace llm {

class ILLMClient;

class LLMClientFactory {
public:
    // 构造工厂，持有 provider 配置的私有副本（构造后不可变）。
    //
    // @param config provider 配置，按值传入并移动保存。
    explicit LLMClientFactory(ProviderConfig config);

    // 创建新的 LLMClient 实例。
    // 每次调用返回独立实例，调用方可安全地在不同线程并发使用。
    //
    // @return 新建实例的拥有权指针，生命周期完全交给调用方。
    std::unique_ptr<ILLMClient> create() const;

    // 返回当前 ProviderConfig（只读）。
    const ProviderConfig& config() const { return config_; }

private:
    ProviderConfig config_;
};

} // namespace llm
