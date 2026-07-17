# ToolCallLoop 超时机制问题 — 2026-07-17

## 问题

`ToolCallLoop::run()` 中有一段超时控制逻辑，存在两个问题：

### 1. 概念上不合适

写小说是复杂的长时间任务，LLM 调用 + 工具执行可能耗时很长。在循环层设一个硬超时，可能会在正常执行过程中被中断，导致任务半途而废。已有 `max_rounds` 作为最大轮数兜底，额外加超时没有提供实质性的安全保障。

### 2. 超时后僵尸线程导致 UB

```cpp
if (config.timeout.count() > 0) {
    auto future = std::async(std::launch::async, executeLoop);
    if (future.wait_for(config.timeout) == std::future_status::timeout) {
        // ⚠️ executeLoop 仍在后台运行！
        // 它 [&] 捕获了 conversation、tools、response、call_history、r 等局部引用
        // run() 返回后这些局部变量被销毁，后台线程访问悬空引用 → 未定义行为
        result.timed_out = true;
        return result;
    }
    return future.get();
}
```

`std::async` 超时后**不会杀死后台线程**。`executeLoop` lambda 通过 `[&]` 捕获了大量局部变量的引用（`conversation`, `tools`, `response`, `call_history`, `r` 等），一旦 `run()` 返回，这些变量被销毁，后台线程就会读写悬空内存。

## 解决方案

**直接删除整个 timeout 机制。**

理由：
- `max_rounds` 已提供最大轮数限制，足够防止无限循环
- `cancelled_` 信号提供了外部安全取消能力（SubAgent 使用）
- 超时带来的僵尸线程 UB 风险远大于其收益
- 项目中目前也没有任何地方传入非零的 `config.timeout`

### 需要删除的内容

- `ToolCallLoopConfig::timeout` 字段
- `ToolCallLoopConfig::setTimeout()` setter
- `ToolCallLoopResult::timed_out` 字段
- `run()` 末尾的 `if (config.timeout.count() > 0)` 分支
- `<future>` include（如果不再使用）
- 注释中涉及 "超时" 的说明
