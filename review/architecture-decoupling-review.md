# 架构解耦审查报告

日期: 2026-07-22
范围: Agent 层设计与开发视角，识别适合解耦的耦合点

---

## 1. SearchMemoryTool 全局静态指针（优先级：高）

**位置:** `src/agent/tools/SearchMemoryTools.cpp:22-23`

**现状:**
```cpp
std::atomic<retrieval::IVectorStore*>        g_vector_store{nullptr};
std::atomic<retrieval::IEmbeddingGenerator*> g_embedding_gen{nullptr};
```

通过 `initSearchMemoryBackend()` 在 `NovelAgentApp::setupAgent()` 中设置全局指针，
`SearchMemoryTool::execute()` 在运行时从全局状态读取。

**问题:**
- 服务定位器反模式：依赖关系完全隐藏，看构造函数无法知道工具需要什么
- 无法单元测试（必须设置全局状态才能执行）
- 多实例场景下会互相覆盖
- 初始化时序脆弱（必须在 registerAllTools 之前调用 initSearchMemoryBackend）

**建议:**
让 SearchMemoryTool 通过构造函数接收 `IVectorStore&` 和 `IEmbeddingGenerator&`，
跟其他工具接收 `shared_ptr<Project>` 一样。扩展工厂签名或引入 ToolDependencies 结构体。

**收益:** 可测试性、依赖显式化、消除初始化时序问题

---

## 2. ContextManager 职责过重（优先级：高）

**位置:** `src/agent/context/`（ContextManager 已拆分为 Compactor / ContextBudgetEvaluator / TokenBudget 等组件）

**现状:** 原 ContextManager 承担 5 个独立职责：

| 职责 | 方法 |
|------|------|
| 构建动态 system prompt | `buildSystemPrompt()` / `assemble()` |
| Token 追踪 + 阈值检查 | 委托 `TokenTracker` |
| 对话压缩（LLM 摘要） | `compact()` + `buildCompactPrompt()` |
| 会话持久化 | 委托 `SessionPersistence` |
| Token 校准 | `calibrator_` + `estimateTokens()` |

**问题:**
改压缩逻辑要看 ContextManager，改持久化也要看 ContextManager，改 prompt 构建还是看它。
阅读时很难快速定位目标代码。

**建议:**
将压缩逻辑提取为独立的 `ConversationCompactor` 类（持有 `summary_`、`marker_`、`auto_compact_`），
ContextManager 持有它并委托。ContextManager 变成纯粹的"上下文组装协调器"。

**收益:** 单一职责、可读性、压缩策略可独立演进和测试

---

## 3. Agent 会话管理职责可分离（优先级：中）

**位置:** `src/agent/core/Agent.h` / `src/agent/core/Agent.cpp`

**现状:** Agent 混合了：
- LLM 交互编排（processSerial / processParallel / buildEffectivePrompt）
- 会话生命周期管理（saveSessionState / loadSessionState / rewindTo / checkpointIndices / pinMessage）
- 可观测性（tracer_）
- 状态机（state_）

会话生命周期管理（~60 行）本质上是 Conversation + ContextManager 的组合操作，
跟"调用 LLM 并执行工具"这个核心职责无关。

**建议:**
提取 `SessionManager`（或扩展现有 SessionPersistence）封装 rewindTo、pin/unpin、save/load 逻辑。
Agent 只保留 process() / execute() 核心路径，通过组合调用 SessionManager。

**收益:** 降低 Agent 认知负担，process() 方法更聚焦

---

## 4. processSerial 中 Token 校准回调耦合（优先级：中）

**位置:** `src/agent/core/Agent.cpp:251-266`

**现状:**
```cpp
config.hooks.on_round_complete = [this, &conversation](int input, int output, int estimated) {
    context_manager_->recordUsage(input, output);
    if (context_manager_->hasCalibrator() && estimated > 0 && input > 0) {
        context_manager_->calibrator()->calibrate(...);
    }
    auto status = context_manager_->checkThresholds();
    if (status.status >= ContextStatus::AutoCompact ...) {
        context_manager_->compact(conversation, *client_, ...);
    }
};
```

**问题:**
Agent 知道了太多 ContextManager 的内部细节（calibrator、thresholds、compact 触发条件）。
这些是 ContextManager 自己的策略，不应该由 Agent 来编排。

**建议:**
让 ContextManager 提供 `onRoundComplete(conversation, client, input, output, estimated)` 方法，
内部自行决定校准和压缩。Agent 的 hook 只需一行调用。

**收益:** 减少 Agent 对 ContextManager 内部的耦合，策略变更不影响 Agent

---

## 5. BuiltInTool 全局工厂注册（优先级：低）

**位置:** `src/agent/tools/BuiltInTool.h` — REGISTER_TOOL / REGISTER_TOOL_NP 宏

**现状:**
依赖 C++ 动态初始化顺序（代码中已标注 Issue 19）。全局 `factories()` 是 Meyers' Singleton。
当前安全是因为工具构造函数无交叉依赖，但随着工具增多约束容易被打破。

**问题:**
- 静态初始化顺序未定义（跨编译单元）
- REGISTER_TOOL_NP 是特殊路径（不需要 Project 的工具），工厂签名不统一
- 无法在注册点注入额外依赖（如 VectorStore）

**建议（两个方向）:**

方向 A — 显式注册：在 AgentSetup.cpp 的 registerAllTools() 中逐个 make_unique 并注册。
失去自动注册便利，但初始化顺序确定、依赖注入显式。

方向 B — 统一工厂签名：引入 `ToolDependencies` 结构体（含 Project、IVectorStore*、IEmbeddingGenerator*），
工厂签名统一为 `Factory(ToolDependencies&)`，消除 REGISTER_TOOL_NP 特殊路径。

**收益:** 消除静态初始化风险，依赖注入统一化

---

## 总结

| 优先级 | 改动 | 核心收益 |
|--------|------|----------|
| 高 | 消除 SearchMemoryTool 全局指针 | 可测试性、依赖显式化 |
| 高 | ContextManager 提取 Compactor | 单一职责、可读性 |
| 中 | Agent 会话管理分离 | 降低 Agent 认知负担 |
| 中 | Token 校准回调内聚到 ContextManager | 减少跨层耦合 |
| 低 | 工具注册改为显式/统一签名 | 消除静态初始化风险 |
