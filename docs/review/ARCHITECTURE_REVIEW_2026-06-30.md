# Agent 架构审查 — 待处理问题清单

> 源：2026-06-30 独立 Agent 架构审查（ad4f9e536f8bfcb0c）
> 已即时修复（第一批）：反射消息角色伪造、ParallelProcessor 上下文盲区
> 已即时修复（第二批 A 批）：A1 串行线程创建、A2 持久化（已验证安全的现有行为）、A3 SubAgent 轨迹可见性、A4 ReAct 预思考步骤
> 以下为需后续安排的遗留问题，按优先级排列

---

### 批次 A：高优先级（影响正确性/数据完整性）

> ✅ 已全部处理（A1 串行线程 ✅、A2 已验证安全 ✅、A3 SubAgent 轨迹 ✅、A4 预思考 ✅）
> 未见未处理的 A 批问题

### 批次 B：中优先级（代码质量/性能）

| # | 问题 | 模块 | 描述 | 建议方案 |
|---|------|------|------|---------|
| B1 | `isParallelEnabled()` 用 `dynamic_cast` | `Agent.cpp:261` | 若 processor 被包装则 dynamic_cast 静默失败 | 在 `IMessageProcessor` 接口上加 `bool isParallel() const` 虚方法 |
| B2 | 无模板时并行降级为单子任务包装 | `AgentOrchestrator.cpp:44-53` | 无 TemplateManager 时创建单一 SubAgent 有受限工具集，增加了开销但没有价值 | 空分解回退到串行模式 |
| B3 | `TokenTracker::record()` 只记 input_tokens | `TokenTracker.h:23` | `current_context_size_` 应设为 `input + output`，否则 `usagePercent()` 偏低 | `current_context_size_ = input_tokens + output_tokens;` |
| B4 | `Conversation::messages()` 每次深度复制 | `Conversation.h:88-97` | 长对话每次调用都深度复制所有字符串，ToolCallLoop 每轮多次调用 | 添加 `messagesView()` 返回 `std::span<const Message>` |
| B5 | `PromptComposer` 职责过薄 | `PromptComposer.h` | 实际只做字符串拼接，不值得独立类 | 内联到 ContextManager 或注入更多职责 |
| B6 | `ExecutionTracer` payload 非强类型 | `ExecutionTracer.h:44` | `nlohmann::json payload` 无结构化约束 | 为常见事件类型添加结构化负载类型 |

### 批次 C：低优先级（小改进/边缘情况）

| # | 问题 | 模块 | 描述 | 建议方案 |
|---|------|------|------|---------|
| C1 | 硬编码 300s 超时 | `IMessageProcessor.cpp:119` | `config.timeout = 300s` 应源自配置 | 从 `max_tool_rounds` 推算或从 AppConfig 读取 |
| C2 | `REGISTER_TOOL` 静态初始化器膨胀 | 工具 `*.cpp` | 每个工具文件添加一个静态初始化变量 | 工具数超 20-30 后迁移到 `constinit`/`constexpr` 注册 |
| C3 | `ConversationDiff::pinned_indices` 偏移风险 | `Conversation.h:174-183` | 假设 apply 顺序固定，无编译期不变量保护 | 添加静态断言或注释标注不变量 |
| C4 | `RewindTo` 丢弃 pinned 消息但只警告 | `Agent.cpp:121-136` | 记录警告但仍丢弃 | 拒绝回滚或自动添加丢弃总结 |
| C5 | ShellTools 别名覆盖不全 | `ShellTools.cpp:52-53` | PowerShell 数百个别名，白名单只覆盖 13 个常见别名 | 建立 cmdlet→别名映射表自动扩展 |
| C6 | ThreadPool.h 注释是英文 | `ThreadPool.h` | 违反 CLAUDE.md 中文注释规则 | 汉化注释 |

---

## 各批次关联问题

- **A1** 与 **C1** 相关：都涉及超时机制，建议一并修复
- **B1** 与 **B5** 都是接口设计问题，建议在下次接口重构时统一处理
- **A3** 与 **B6** 相关：ExecutionTracer 的改进会影响 SubAgent 轨迹冒泡的方案设计
