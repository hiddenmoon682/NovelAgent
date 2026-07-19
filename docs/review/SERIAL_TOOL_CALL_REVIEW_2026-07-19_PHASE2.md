# 串行工具调用流程二次审查报告

> 审查日期：2026-07-19（第二轮）
> 审查范围：`Agent::processSerial()` → `ToolCallLoop::run()` → `ToolPipeline::execute()` → `Conversation::apply()`
> 审查方式：10 角度独立分析（行级扫描 / 行为移除审计 / 跨文件追踪 / 语言陷阱 / 封装器正确性 / 代码复用 / 简化 / 性能 / 设计深度 / 代码约定）+ 15 路独立验证（1 票制）+ 1 轮 sweep 扫描
> 上次审查：[SERIAL_TOOL_CALL_REVIEW_2026-07-19.md](SERIAL_TOOL_CALL_REVIEW_2026-07-19.md)（15 个发现已全部修复）

---

## 严重缺陷（HIGH）

### #1 — process() 异常路径不撤销 conversation 修改

**位置**：`Agent.cpp:483-489`（catch 块）

**问题**：`process()` 的顶层 catch 块仅恢复状态机（`transition(Error)` → `recover()`），不做任何 conversation 回滚。但异常发生前 `processSerial()` 可能已修改了 `conversation_`：

1. `processSerial:241` — 追加了 user 消息
2. `processSerial:247` → `buildEffectivePrompt:214` → `assemble()` — 可能触发了自动 `compact()`（`ContextManager.cpp:359`），执行了 `removeOldest` + `prepend`
3. `processSerial:274` → `loop.run()` — 可能执行了多轮工具调用，每轮添加 assistant + tool_result 消息

异常后对话状态不可恢复，导致后续 LLM 调用看到乱序或不一致的对话序列。

**触发条件**：串行处理过程中任意环节抛出异常（网络故障/内存不足/API 错误等）。

**影响**：
- 用户消息孤立在对话中无对应回复
- 部分工具执行记录残留
- 压缩摘要可能已替换旧历史但元数据不一致
- 下一个用户输入追加后 LLM 看到两端式对话

**验证**：CONFIRMED

---

### #2 — Conversation::apply() 部分执行无回滚

**位置**：`Conversation.h:208-219` + `ToolCallLoop.cpp:169-172`

**问题**：`apply()` 遍历 `diff.added` 逐个 `push_back`，每个 `push_back` 都可能抛出 `bad_alloc`。第 N 个失败时前 N-1 个已提交到 `messages_`。`ToolCallLoop` 的 catch 块只调一次 `popBack()`（移除最后添加的 assistant 消息），无法清除已提交的部分 tool_result。

**触发条件**：多工具调用的 diff 在 `apply()` 中途触发 `bad_alloc`（消息总量接近内存上限时）。

**影响**：
- 对话中残留孤立 tool_result 消息（无对应 assistant(tool_calls)）
- 消息序列违反 API 协议（assistant → tool_result 配对规则）
- LLM API 可能拒绝该请求

**验证**：CONFIRMED

---

### #3 — truncateResult UTF-8 截断残留不完整 leading byte

**位置**：`ToolPipeline.cpp:97-102`

**问题**：`content.resize(kMaxContentChars)` 按字节截断可能落在多字节 UTF-8 字符的中间。后续 while 循环只反向弹出 continuation bytes（`0x80-0xBF`），但 leading byte（`0xC0-0xF7`）不会被弹出，结果字符串末尾残留孤立 leading byte，产生非法 UTF-8 序列。

**触发条件**：工具结果中多字节字符（中文/日文/韩文等）出现在 56000 字节边界附近。

**影响**：非法 UTF-8 序列 → 序列化为 JSON 后可能被 LLM API 拒绝 → 工具调用结果丢失。

**验证**：CONFIRMED

---

### #4 — isRepeatedCall 使用原始 JSON 字符串做 key，键顺序导致漏检

**位置**：`ToolCallLoop.cpp:37`

**问题**：重复检测 key 为 `tool_name + ":" + args_json`（原始 JSON 字符串，不做任何解析/归一化）。LLM 返回的 JSON 键顺序不保证稳定（`{"a":1,"b":2}` vs `{"b":2,"a":1}`），相同语义的参数因不同序列化顺序生成不同 key，各自独立计数，永远达不到 `max_repeated_calls` 阈值。

**触发条件**：LLM 连续调用同一工具但参数 JSON 键顺序不同（实践中常见，尤其多字段参数）。

**影响**：重复检测形同虚设 → LLM 可能陷入无限调用同一工具的循环。

**验证**：CONFIRMED

**建议方案（待审查）**：JSON parse + dump 归一化键顺序，消除不同序列化顺序产生的 key 差异。`nlohmann::json::dump()` 以 `std::map` 迭代输出，键为字典序。

```cpp
std::string normalized;
try {
    normalized = nlohmann::json::parse(args_json).dump();
} catch (...) {
    normalized = args_json;
}
std::string key = tool_name + ":" + normalized;
```

与哈希方案的对比：parse + dump 同样解决键顺序问题，无碰撞风险，代码更简单直接。在 `run()` 局部 `call_history` 仅有数十条记录的场景下，哈希的内存/查找优势不可见，直接使用字符串 key 即可。

---

### #5 — compact() 异常清空上次成功的摘要元数据

**位置**：`ContextManager.cpp:257-266`

**问题**：`compact()` 的 catch 块无条件 `summary_.clear()` + `marker_ = 0`。但 conversation 的 `removeOldest` + `prepend`（行 238-244）只在 LLM 调用成功后执行。如果 `llm_client.chatNonStreaming()` 抛出异常（瞬态网络错误），conversation 未修改，但摘要元数据丢失。

**触发条件**：第二次 `compact()` 的 LLM 调用失败（网络超时/API 500 等）。

**影响**：
- `saveSessionState()` 持久化空摘要 → 下次加载丢失压缩状态
- `/rewind` 的 compaction boundary 检查（`compactionMarker()`）返回 0 → 判断失效
- 对话中残留旧摘要消息但系统标记为未压缩

**验证**：CONFIRMED

---

## 中危缺陷（MED）

### #6 — Token 校准忽略 system prompt，系统性偏差

**位置**：`ToolCallLoop.cpp:60-61`

**问题**：`estimated = TokenCounter::countMessages(conversation.messages())` 只统计非 system 消息（`messages()` 排除 `system_prompt_`）。但 API 返回的 `prompt_tokens` 包含 system prompt。每轮校准 `calibrate(estimated, actual)` 收到 `estimated < actual` 的系统性偏差。

**触发条件**：任何需要 token 估算的 LLM 调用。

**影响**：系统 prompt 越长偏差越大 → EMA 校正因子 drift → 后续 token 估算被放大 → 自动压缩阈值误触发或上下文不足告警误报。

**验证**：CONFIRMED

---

### #7 — content.resize 使用字节限制而非字符限制

**位置**：`ToolPipeline.cpp:97`

**问题**：`kMaxContentChars = 56000` 是字节上限，但 `std::string::resize(n)` 按字节操作。对于 CJK 文本（3 字节/字符），实际截断为 ~18666 字符而非预期的 56000 字符。常量名 `kMaxContentChars` 和注释 "字符上限" 暗示限制级别。

**触发条件**：中文/日文/韩文等三字节 UTF-8 文本的工具结果。

**影响**：CJK 内容被过度截断 → 长中文章节只剩三分之一 → LLM 看到的上下文信息量大幅降低。

**验证**：CONFIRMED

---

### #8 — rounds_executed 少计 1 轮

**位置**：`ToolCallLoop.cpp:103`

**问题**：LLM 无工具调用时的早退路径设 `r.rounds_executed = round`（0-indexed），但其他所有路径使用 round+1：
- 重复检测路径（行 156）：`round + 1`
- 最大轮数路径（行 180）：`config.max_rounds`

**触发条件**：LLM 首次调用就直接返回文本（无工具调用）。

**影响**：`rounds_executed` 为 0 但实际执行了 1 轮 LLM 调用，统计信息失真。

**验证**：CONFIRMED

---

### #9 — cancelled_ 检查在 chat() 调用之后

**位置**：`ToolCallLoop.cpp:59-77`

**问题**：`cancelled_` 检查在 `client_.chat()` 返回之后（行 77），而非每轮循环开始处（行 59）。头文件注释（`ToolCallLoop.h:68`）声称 "在每轮循环开始处检查" 与实际不符。取消信号至少需要一次完整的 LLM 往返才能被响应。

**触发条件**：用户在 LLM 请求过程中主动取消。

**影响**：每轮浪费一次 LLM API 调用的 token 开销，在轮数多时累积浪费显著。

**验证**：CONFIRMED

---

### #10 — on_round_complete hook 硬编码 conversation_

**位置**：`Agent.cpp:256-270`

**问题**：`processSerial()` 的 `on_round_complete` 回调通过 `[this]` 捕获 `this->conversation_`，其中的 `compact()` 调用硬编码了成员变量而非形参。当前所有调用方传入的都是 `conversation_`，所以表现正确。但未来重构传入其他 conversation 时，hook 会压缩错误的对话。

**触发条件**：未来重构将 `processSerial()` 改为接受非成员 conversation。

**影响**：hook 中的 compact 操作在错误的对象上 → 两对话数据损坏。

**验证**：CONFIRMED

---

## 低危缺陷 & 代码清理

### #11 — 取消退出路径不设置 rounds_executed

**位置**：`ToolCallLoop.cpp:77-98`

**问题**：取消路径在返回前不设置 `r.rounds_executed`，保持默认值 0。其他所有退出路径都明确设置了该字段（行 103/156/180）。

**触发条件**：多轮工具执行后用户取消。

**影响**：调用方认为零轮执行，掩盖了已完成的轮次。

**验证**：CONFIRMED

---

### #12 — std::set 固定 7 元素集合效率欠佳

**位置**：`ToolPipeline.cpp:17-22`

**问题**：`static const std::set<std::string> kSettingTools` 为 7 个固定字符串的动态集合，每次首次调用触发堆分配和红黑树构建。可用 `constexpr std::array` + 线性搜索在编译期完成，零运行时开销。

**触发条件**：每次 `ToolPipeline::execute()` 调用。

**影响**：非致命性能开销，但在百轮工具循环中有累积。主耗时在 LLM API 调用，此条影响有限。

**验证**：PLAUSIBLE

---

### #13 — kCompactKeepExchanges 注释过期

**位置**：`ContextManager.cpp:76, 169`

**问题**：`kCompactKeepExchanges = 3`，但注释写着 "保留最近 10 对 = ~20 条消息"。`ideal_keep` 计算为 `3 * 2 = 6` 而非 20。常量值在重构中从 10 改为 3 时未更新注释。

**触发条件**：任何阅读该代码的人。

**影响**：误导维护者，可能导致对压缩策略的误判。

**验证**：CONFIRMED

---

### #14 — rewindTo 窄化转换潜在溢出

**位置**：`Agent.cpp:141`

**问题**：`static_cast<int>(index + 1)` 将 `size_t`（64-bit）窄化为 `int`（32-bit）。理论上当 `index + 1 > INT_MAX`（~21.5 亿条消息）时结果为负数，与 `marker`（正数）比较恒为真，误触发 `clearCompactedSummary()`。

**触发条件**：仅理论场景，实际需要 2^31+ 条消息。

**影响**：压缩摘要被无故清空 → 下次请求时强制重新压缩。

**验证**：PLAUSIBLE

---

### #15 — truncateResult 死代码

**位置**：`ToolPipeline.cpp:126` / `ToolPipeline.h:49`

**问题**：`truncateResult()` 已声明并实现，但 grep 确认全项目无任何调用点。当前工具结果截断由 `executeOne()` 内的 UTF-8 截断逻辑直接完成。

**触发条件**：代码阅读/维护。

**影响**：增加维护复杂度，读者可能误认为有调用点。

**验证**：CONFIRMED

---

## 附录：审查方法

| 方向 | 角度 | 分析数 | 验证数 |
|------|------|--------|--------|
| A | 行级 bug 扫描 | 6 候选 | — |
| B | 行为移除审计 | — | — |
| C | 跨文件追踪 | 8 候选 | — |
| D | 语言陷阱 | 5 候选 | — |
| E | 封装器/代理正确性 | 8 候选 | — |
| 复用 | 代码可复用性 | — | — |
| 简化 | 不必要复杂度 | — | — |
| 性能 | 浪费的工作 | — | — |
| 深度 | 实现深度 | — | — |
| 约定 | CLAUDE.md 合规 | — | — |
| 验证 | 1 票制 3 态验证 | — | 15 |
| Sweep | 遗漏扫描 | 5 新增 | 3 |
| **合计** | | **32 → 去重 15** | **15 验证** |
