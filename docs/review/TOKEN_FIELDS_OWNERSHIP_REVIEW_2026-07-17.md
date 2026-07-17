# ToolCallLoopResult token 字段归属问题 — 2026-07-17

## 问题

`ToolCallLoopResult` 中的 3 个 token 计数字段存在职责不清、消费者分散的问题。

## 现状

```cpp
struct ToolCallLoopResult {
    int total_tokens_used = 0;     // 所有轮次累计 token 消耗
    int input_tokens = 0;          // 累计 prompt_tokens
    int output_tokens = 0;         // 累计 completion_tokens
};
```

### 各字段的实际消费者

| 字段 | 消费者 | 用途 |
|------|--------|------|
| `input_tokens` | `SubAgent.cpp:60` → `AgentOrchestrator.cpp:191` | SubAgent 结果汇总（orchestrator 统计子任务 token 消耗） |
| `output_tokens` | `SubAgent.cpp:61` → `AgentOrchestrator.cpp:192` | 同上 |
| `total_tokens_used` | **仅** `ToolCallLoop.cpp` 内部的 `token_warning_threshold` 检查 | 无外部消费者，且等于 `input_tokens + output_tokens` |

### 问题点

1. **`total_tokens_used` 冗余** — 没有任何外部代码读取它，仅内部用于 warning 检查，而 `input_tokens + output_tokens` 语义等价
2. **`input_tokens` / `output_tokens` 放在这里有争议** — `Agent` 路径完全通过 `on_round_complete` hook 记录 token，不读 result 中的这些字段。只有 `SubAgent` 路径因为没有 hook 机制，需要通过 result 传递出来
3. **SubAgent 和 Agent 路径 token 记录方式不统一**：
   - Agent：hook → `context_manager_->recordUsage(input, output)` 实时记录
   - SubAgent：result → `SubAgentResult.input_tokens/output_tokens` → 等待 Orchestrator 处理
4. **`// 供 ContextManager::recordUsage 使用` 注释不准确** — 没有任何代码真的从 `ToolCallLoopResult` 调用 `ContextManager::recordUsage`

## 建议方向

- `total_tokens_used` → 删除，用 `input_tokens + output_tokens` 替代
- `input_tokens` / `output_tokens` → 保留在 `ToolCallLoopResult` 中（SubAgent 路径仍需要），更新注释准确描述实际消费者
- 或给 SubAgent 也传入 `TokenTracker*` 指针，从源头消除中转需求
