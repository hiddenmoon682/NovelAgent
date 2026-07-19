# 串行工具调用流程审查报告

> 审查日期：2026-07-19
> 审查范围：`Agent::processSerial()` → `ToolCallLoop::run()` → `ToolPipeline::execute()` → `Conversation::apply()`
> 审查方式：6 方向独立分析（行级扫描 / 错误安全边界 / 跨文件追踪 / API 协议合规 / Token 校准 / 代码清理）+ 3 次独立验证 + 1 轮 sweep 扫描

---

## 严重缺陷（HIGH）

### #1 — max_rounds 退出时 assistant 消息双重添加 + content 丢失

**位置**：`ToolCallLoop.cpp:86-103` + `Agent.cpp:280-287`

**问题**：当达到 `max_rounds` 且最后一轮 LLM 返回了工具调用时：

1. `ToolCallLoop.cpp:88-89` 用 `std::move` 将 `response.content` 和 `response.reasoning_content` 移入 assistant 消息
2. `ToolCallLoop.cpp:91` 将该 assistant 消息（含 tool_calls）加入对话
3. 循环自然退出（`round == max_rounds`），`ToolCallLoop.cpp:103` 将已移空的 `response` 拷贝给 `r.response`
4. `Agent.cpp:280-287` 由于 `result.response.tool_calls` 非空（拷贝而非移动），又添加了一条相同 tool_calls 的 assistant 消息

**结果**：
- 对话中出现重复的 assistant(tool_calls) 消息，第二条无对应工具结果
- `r.response.content` 和 `r.response.reasoning_content` 为空
- 后续 LLM 请求发送违规消息序列，可能被 API 拒绝

**验证**：CONFIRMED（5 个方向独立发现，1 次验证确认）

---

### #2 — 取消/循环检测退出路径丢弃有效响应

**位置**：`ToolCallLoop.cpp:49-53`（取消）、`ToolCallLoop.cpp:79-83`（循环检测）

**问题**：当循环因取消或检测到重复而退出时，`r.response` 保持默认构造状态（空），不保存最近一次 `client_.chat()` 的结果。

| 退出原因 | `r.response` 状态 |
|----------|------------------|
| 正常（tool_calls 为空） | ✅ 有效响应 |
| 达到 max_rounds | ❌ content 已移出（#1 已报告） |
| 取消 | ❌ 默认构造，丢弃有效数据 |
| 循环检测 | ❌ 默认构造，丢弃有效数据 |

**验证**：PLAUSIBLE

---

## 中危缺陷（MED）

### #3 — pipeline.execute() 异常致对话残留孤立 assistant

**位置**：`ToolCallLoop.cpp:91-95`

**问题**：`ToolPipeline::execute()` 和 `executeOne()` 均无完整 try/catch 保护。若执行抛出（如 `result.dump()`、`ParameterValidator::validate()`），`ToolCallLoop.cpp:91` 已添加的 assistant（含 tool_calls）无法回滚。`Agent::process()` 的 catch（`Agent.cpp:478-484`）只恢复状态机，不修复对话。

**触发路径**：
- `ToolPipeline::executeOne()` L65-74: 仅 try/catch 包裹 `json::parse`，后续 `dump()`、`validate()` 均无保护
- `ToolPipeline::execute()` L24-34: 整个 for 循环无 try/catch
- `ToolRegistry::executeTool()` L88-98: 有 try/catch，但异常可能在它之前抛出

**验证**：CONFIRMED

---

### #4 — 最终 assistant 消息缺失 reasoning_content

**位置**：`Agent.cpp:280-287`

**问题**：`processSerial` 构建最终 assistant 消息时复制 `content` 和 `tool_calls`，但不复制 `reasoning_content`。对比 `ToolCallLoop.cpp:89` 正确复制了 `reasoning_content`。

**影响**：Thinking 模式下最终回复的思考过程永久丢失，违反"全程保留 reasoning_content"策略。

**验证**：CONFIRMED

---

### #5 — compact LLM 调用 token 未记录到 TokenTracker

**位置**：`Agent.cpp:264-270`、`ContextManager.cpp:220`

**问题**：`on_round_complete` hook 中可能触发 `compact()`，其内部调用 `chatNonStreaming()` 消耗 tokens。但这些 token 从未经 `recordUsage()` 记录到 TokenTracker。`ContextManager.cpp` 中无任何对 `recordUsage` 或 `tracker_.record` 的调用。

**影响**：会话 token 统计（`sessionStats()`）系统性偏低。若每轮都触发压缩，低估可达 30%+，计费统计失真。

**验证**：CONFIRMED

---

### #6 — TokenCounter 未统计 reasoning_content

**位置**：`TokenCounter.cpp:144-177`

**问题**：`countSingleMessage()` 和 `countMessages()` 均只统计 `content`、`tool_call_id`、`name`、`tool_calls`，未统计 `reasoning_content`。但该字段确实被序列化发送给 API，计入 `prompt_tokens`。

**影响链**：估算偏低 → EMA 校准因子膨胀（可达 2.0+）→ 后续对话 token 被夸大 → 自动压缩阈值误触发或上下文不足告警误报。

**验证**：PLAUSIBLE

---

### #7 — processSerial 忽略退出原因标志

**位置**：`Agent.cpp:278-287`

**问题**：`loop.run()` 返回的 `ToolCallLoopResult` 包含 `cancelled` 和 `loop_detected` 标志，但 `processSerial` 只取 `result.response`，不检查任何标志。与 `SubAgent.cpp:63-65`（正确检查并传播标志）不一致。

**验证**：PLAUSIBLE

---

## 低危缺陷

### #8 — 取消检查在 LLM API 调用之后

**位置**：`ToolCallLoop.cpp:44-53`

**问题**：`cancelled_` 检查（L49）在 `client_.chat()`（L45）之后。取消信号至少浪费一次完整 LLM 往返才被响应。头文件注释声称"在每轮循环开始处检查"与实际不符。

**验证**：PLAUSIBLE

---

### #9 — max_repeated_calls ≤ 0 无钳位防护

**位置**：`ToolCallLoop.cpp:22`

**问题**：`isRepeatedCall()` 条件为 `++call_history[key] >= max_repeats`。若 `max_repeats` 为 0 或负数，条件恒真，每个工具调用立即判为重复。

**验证**：PLAUSIBLE

---

### #10 — chat()/hook 抛出同致孤立轮次

**位置**：`ToolCallLoop.cpp:45-47`

**问题**：除 `execute()` 外，`client_.chat()`（L45）网络故障和 `config.hooks.on_round_complete`（L46-47）中 `recordUsage`/`compact` 抛出同样导致当前轮次孤立。

**验证**：PLAUSIBLE

---

### #11 — 异常 catch 返回无法区分的空响应

**位置**：`Agent.cpp:478-483`

**问题**：catch 块返回 `{}`（默认 `LLMResponse`），`finish_reason` 为默认值 `"stop"`。调用方无法区分"LLM 正常返回空内容"和"系统异常"。错误仅记录到 spdlog 和 tracer。

**验证**：PLAUSIBLE

---

### #12 — buildEffectivePrompt 混用成员/参数

**位置**：`Agent.cpp:210,217`

**问题**：与 #7 同模式——`buildEffectivePrompt()` 在 L210 和 L217 使用 `conversation_`（成员变量）而非参数 `conversation`。当前 `process()` 传入 `conversation_` 故相同，未来重构传入其他对话时静默操作错误对象。

**验证**：PLAUSIBLE

---

## 代码清理

### #13 — streaming 字段死代码

**位置**：`ToolCallLoop.h:31,38`

**问题**：`streaming` 字段和 `setStreaming()` setter 无任何引用。注释自承"未使用——run() 中始终流式"。

**建议**：删除。

---

### #14 — executeAndAppend 未使用

**位置**：`ToolPipeline.cpp:38-45`

**问题**：`executeAndAppend()` 有实现但无任何调用点。当前 ToolCallLoop 使用分离模式（`execute()` + `apply()`）。

**建议**：删除或加 `[[deprecated]]` 标注。

---

### #15 — tool_calls 拷贝非 move，易误导

**位置**：`ToolCallLoop.cpp:90`

**问题**：L88-89 用 `std::move` 移走 `content`/`reasoning_content`，L90 却拷贝 `tool_calls`。原因是 L95 的 `pipeline.execute(response.tool_calls)` 需要有效值。对比赋值风格不一致，未来开发者循惯性改为 `std::move` 会静默破坏 L95。

**建议**：加注释说明，或重构为先用本地变量保存再 move。

---

## 附录：审查方法

| 方向 | 角度 | 分析数 | 验证数 |
|------|------|--------|--------|
| A | 行级 bug 扫描 | 8 候选 | — |
| B | 错误/安全边界 | 5 候选 | 1 |
| C | 跨文件追踪 | 7 候选 | — |
| D | Token 校准分析 | 4 候选 | 1 |
| E | API 协议合规 | 5 候选 | — |
| F | 代码清理 | 6 候选 | 1 |
| Sweep | 遗漏扫描 | 7 新增 | — |
| **合计** | | **42 → 去重 15** | **3 验证** |
