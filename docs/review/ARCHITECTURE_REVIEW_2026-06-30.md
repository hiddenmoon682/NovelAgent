# Agent 架构审查 — 待处理问题清单

> 源：2026-06-30 独立 Agent 架构审查（ad4f9e536f8bfcb0c）
> 已关闭问题移至 `ARCHITECTURE_REVIEW_RESOLVED.md`
> 以下仅为尚待处理的遗留问题

---

### B 批：中优先级（代码质量/性能）

| # | 问题 | 模块 | 描述 | 建议方案 |
|---|------|------|------|---------|
| B2 | 无模板时并行降级为单子任务包装 | `AgentOrchestrator.cpp:44-53` | 无 TemplateManager 时创建单一 SubAgent 有受限工具集，增加了开销但没有价值 | 空分解回退到串行模式 |

### C 批：低优先级（小改进/边缘情况）

| # | 问题 | 模块 | 描述 | 建议方案 |
|---|------|------|------|---------|
| C1 | 硬编码 300s 超时 | `IMessageProcessor.cpp:119` | `config.timeout = 300s` 应源自配置 | 从 `max_tool_rounds` 推算或从 AppConfig 读取 |
| C4 | `RewindTo` 丢弃 pinned 消息但只警告 | `Agent.cpp:121-136` | 记录警告但仍丢弃 | 拒绝回滚或自动添加丢弃总结 |

---

## 各批次关联问题

- **A1** 与 **C1** 相关：都涉及超时机制，建议一并修复（A1 已修复，C1 仍待处理）
- **A3** 与 **B6** 相关：ExecutionTracer 的改进会影响 SubAgent 轨迹冒泡的方案设计（均已修复）
