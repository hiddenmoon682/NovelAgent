# 上下文溢出恢复设计

> 日期：2026-07-21
> 来源：QuantClaw 参考审查 + 后续讨论
> 状态：设计提案（待实现）

---

## 一、问题

### 1.1 场景

当前 `ContextManager::compact()` 是**主动预防型**压缩——在 LLM 调用前根据 token 用量百分比（默认 95%）触发，用 LLM 对旧对话历史做摘要，保留最近约 10 条消息。

存在两种主动预防无法覆盖的溢出场景：

**场景 A：轮内累积溢出**

```
assemble() 时 180K tokens（安全，未达 95% 阈值）
  → 调 LLM → 返回 5 个工具调用，各携大量参数
  → 执行工具 → 结果各 5KB
  → 追加 assistant + tool_result 到 request.messages → 195K tokens → 溢出！
  → 当前行为：throw std::runtime_error ❌
```

`assemble()` 时用量正常，但工具执行后累积的消息使下一轮超限。

**场景 B：估算偏差溢出**

主动预防依赖于估算的 token 数（`estimated_tokens` vs API 实际 `prompt_tokens`）。如果 `TokenCounter` 与 API 计费方式存在系统性偏差（如中文字符计数差异），实际已超限但估算未达阈值，下一轮 `chat()` 直接返回 `context_length_exceeded`。

### 1.2 当前异常传播路径

```
LLM 调用 → HTTP 400 (code: context_length_exceeded)
  → HttpClient::post() 视为不可重试错误，直接 throw
  → LLMClient::chat() throw
  → ToolCallLoop::run() catch → spdlog::error → throw
  → Agent::process() catch → 回滚对话 → 用户输入丢失
```

用户需要重新输入一遍。

### 1.3 恢复手段

overflow 发生后，有两种恢复手段：

| 手段 | 原理 | 优点 | 缺点 |
|------|------|------|------|
| **LLM 压缩** | 调 LLM 对旧消息做摘要 | 保留语义 | 上下文已溢出时，压缩 LLM 自身也可能溢出 |
| **硬截断** | 直接删除旧消息 + 插入系统通知 | 无需 LLM，绝不溢出 | 丢失上下文信息 |

---

## 二、决策原则

截断会**不可逆地丢失上下文**。目前项目没有为截断丢失的信息提供恢复途径（如重新检索、自动填充）。

因此：**截断的决定权应交由用户**，不做自动截断。

### 具体含义

1. 检测到 `context_length_exceeded` 时，给用户一个明确的错误提示，说明上下文已超限
2. 提供两个恢复命令供用户手动选择：
   - `/compact` — LLM 压缩（保留语义）
   - `/truncate [N]` — 硬截断（保留最近 N 对消息）
3. 不自动做任何恢复操作
4. 不丢失用户输入（当前 `Agent::process()` 的快照回滚已保证这一点）

---

## 三、需要修改的点

### 3.1 错误消息区分

当前所有 API 错误都抛 `std::runtime_error`，上层无法区分溢出和其他错误。

**改动**：在 `HttpClient` 或 `LLMClient` 层区分 `context_length_exceeded`，包装为专用异常类型或错误码。

### 3.2 用户反馈

当前溢出时用户看到的只是"API 错误"，不够明确。

**改动**：在 REPL 层给出专门提示：

```
上下文已超限（context_length_exceeded）。
可选操作：
  /compact          压缩旧对话历史（保留语义）
  /truncate [N]     截断旧消息，保留最近 N 对（N 默认 5）
  /clear            清空整个对话
```

### 3.3 新增 `/truncate` 命令

```
用法：/truncate [pairs]
功能：保留最近 pairs 对（用户+助手）消息，删除更早消息
默认：pairs = 5（10 条消息）
实现：Conversation::removeOldest() + prepend 系统通知
```

### 3.4（可选）ToolCallLoop 层溢出重试

若 `ToolCallLoop` 在 `chat()` 中检测到溢出，不直接 throw，而是：
1. 记录错误并停止本轮循环（已有 `result.error` 字段）
2. 让外层 `Agent::processSerial()` 读取结果，设置错误标记
3. 由 REPL 向用户展示恢复选项

这样用户输入不会被快照回滚吞掉，可以直接选择 `/compact` 或 `/truncate`。

---

## 四、不采用的方案及理由

| 方案 | 不采用理由 |
|------|-----------|
| **compact() 内部自动截断兜底** | 截断不可逆，不应在用户不知情时发生 |
| **on_context_overflow 回调+自动截断** | 同上 |
| **CompactOverflow 专用方法+自动截断** | 同上 |
| **HTTP 层自动重试** | 400 有多种含义，不能一概重试；且 HTTP 层无对话引用无法做恢复 |

---

## 五、实现计划（待讨论）

### Phase 1 — 基础（~50 行）

1. `HttpClient` 或 `LLMClient` 层区分 `context_length_exceeded`，抛 `ContextOverflowError`
2. `Agent::processSerial()` 的 catch 块区分 `ContextOverflowError`，设置 `result.error = "context_overflow"`
3. `ReplHandler` 在收到 `context_overflow` 时展示恢复选项

### Phase 2 — 截断命令（~60 行）

1. 新增 `truncateConversation()` 辅助方法
2. 新增 `/truncate` REPL 命令
3. 测试：硬截断后对话格式正确、系统通知已插入

### Phase 3（可选）— ToolCallLoop 层溢出重试（~40 行）

1. `ToolCallLoop` catch `ContextOverflowError` 后停止循环，在 `ToolCallLoopResult` 中传递溢出标记
2. `Agent` 根据标记展示恢复选项（不抛异常、不回滚对话）

---

## 六、相关文件

| 文件 | 改动 |
|------|------|
| `src/llm/HttpClient.h` | 新增 `ContextOverflowError` 或错误码枚举 |
| `src/llm/HttpClient.cpp` | `parseApiError()` 检测 `context_length_exceeded` 并抛专用异常 |
| `src/llm/LLMClient.h/cpp` | catch 专用异常并透传 |
| `src/agent/Agent.cpp` | 区分溢出异常 + 设置错误标记 |
| `src/cli/ReplHandler.cpp` | 展示溢出恢复选项 |
| `src/cli/CommandParser.cpp` | 注册 `/truncate` 命令 |
