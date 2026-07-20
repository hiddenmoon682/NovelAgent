# QuantClaw 参考审查：上下文溢出恢复与流式工具调用

> 日期：2026-07-20
> 来源：参考项目 [QuantClaw](https://github.com/QuantClaw/QuantClaw)（`_ref/QuantClaw/`）
> 审核范围：工具调用循环 + 上下文压缩机制

---

## 发现一：缺乏上下文溢出恢复（被动兜底）

### 现状

我们的 `ContextManager::compact()` 是**主动预防型**压缩——在 LLM 调用前根据 token 用量百分比（默认 95%）触发，用 LLM 对旧对话历史做摘要，保留最近 20 条消息。

但缺少**被动兜底**机制：当 LLM 返回 context overflow 错误时，我们直接抛异常，没有重试。

### 风险场景

```
assemble() 时 180K tokens（安全，未达 95% 阈值）
  → 调 LLM → 返回 5 个工具调用，各携大量参数
  → 执行工具 → 结果各 5KB
  → 追加 assistant + tool_result 到 request.messages → 195K tokens → 溢出！
  → 当前行为：throw std::runtime_error ❌
  → 理想行为：CompactOverflow → retry ✓
```

此场景中 assemble() 时的用量估算合理，但**一轮工具调用执行后**累积的消息量可能超限。主动预防无法覆盖这种情况。

### QuantClaw 的做法

`agent_loop.cpp` 中 `ProcessMessage()` 的 catch 块：

```cpp
catch (const ProviderError& pe) {
    if (pe.Kind() == ProviderErrorKind::kContextOverflow &&
        overflow_retries < kOverflowCompactionMaxRetries) {  // 最多 3 次
        request.messages = engine->CompactOverflow(request.messages, system_prompt, 0);
        continue;  // 重试
    }
    // 3 次失败才 throw
}
```

`CompactOverflow()` 有两种路径：
- **有 LLM 摘要函数**：`MultiStageCompaction` 分块 → 每块摘要 → 合并 → 保留最近 2-4 条
- **无摘要函数**（降级）：截断保留最近一半消息，插一条系统提示

### 建议

在 `ToolCallLoop` 或 `LLMClient` 层增加溢出恢复机制：

1. 区分 context overflow 错误（HTTP 400，错误码含 context_length_exceeded 等）
2. 捕获后调用 `ContextManager::compact()` 或专门的 `CompactOverflow()` 压缩整个 request
3. 压缩后 retry，最多 3 次
4. 仍失败才抛异常

具体位置：`ToolCallLoop.cpp` 的 `run()` 中 `client_.chat()` 调用的 try-catch，或 `HttpClient.cpp` 中根据 HTTP 状态码 + API 错误码区分。

---

## 发现二：流式模式下收到 tool_call trunk 即执行工具

### 现状

我们项目的 `ToolCallLoop` 在流式和非流式模式下都是**等 LLM 完整返回后再执行工具**：

```cpp
// ToolCallLoop.cpp — 非流式
response = client_.chat(conversation.messages(), tools, system_prompt, callbacks);
// response 已完整包含所有 tool_calls → 再执行
auto diff = pipeline.execute(response.tool_calls);
```

流式模式也是类似——`StreamingPipeline` 先完整累积所有 chunk，返回完整 `LLMResponse`，再执行工具。

### QuantClaw 的做法（`ProcessMessageStream`）

在 StreamingLambda **内部**直接执行工具：

```cpp
provider->ChatCompletionStream(request, [&](const ChatCompletionResponse& chunk) {
    // ... 累积 text ...

    if (!chunk.tool_calls.empty()) {
        // 1. 立即构建 assistant_msg（含 tool_use）→ push request.messages
        Message assistant_msg;
        assistant_msg.role = "assistant";
        // ... 追加 thinking/text/tool_use content blocks ...
        request.messages.push_back(assistant_msg);

        // 2. 立即执行每个工具 → push 结果
        Message results_msg;
        results_msg.role = "user";
        for (const auto& tc : valid_tool_calls) {
            auto result = tool_registry_->ExecuteTool(tc.name, tc.arguments);
            // 截断 → 回调推送事件 → push results_msg
            results_msg.content.push_back(ContentBlock::MakeToolResult(tc.id, result));
        }
        request.messages.push_back(results_msg);

        // 3. return（不等流结束，本轮流已处理完毕）
        return;
    }
});
// 流结束后：if (handled_tool_calls) continue; → 下一轮 LLM 调用
```

### 关键区别

| 维度 | 我们 | QuantClaw |
|------|-----|-----------|
| **工具执行时机** | 等 LLM 完整返回后 | 收到第一个 tool_call chunk 时**
| **流是否等完** | 等 finish_reason 才执行 | 立即执行并 return，**不等流结束** |
| **消息追加** | 全部执行完一次追加 | 逐条追加到 request.messages 后立即继续 |
| **代码结构** | 顺序：流完→执行→继续 | **嵌套**：流回调中嵌入执行 |

### 优缺点分析

QuantClaw 的做法：
- **优点**：延迟更低，不需要等整个流结束（尤其 tool_call 出现在末尾时）
- **优点**：回调中的 tool_result 可以实时通过 callback 推送给 UI
- **风险**：LLM 可能在同一个流中先发 tool_call id="123" 再发 id="123" 的完整参数——chunk 级别 tool_call 可能不完整
- QuantClaw 通过 StreamingCallback 由 Provider 层 `accumulateChunk` 保证一个 tool_call 完整时才触发回调

### 建议

当前我们不需要改变，原因：
1. 我们的流式实现 `StreamingPipeline` 已经先完整累积再返回，不丢失 tool_call 内容
2. 等流结束后统一执行工具，代码更清晰
3. 不过可以考虑在回调中**提前推送 "LLM 正在调用工具 X" 的 UI 通知**，降低用户等待感
