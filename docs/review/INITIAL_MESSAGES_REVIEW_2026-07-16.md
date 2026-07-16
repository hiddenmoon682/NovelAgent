# `initial_messages` 参数审查 — 2026-07-16

## 背景

`ToolCallLoop::run()` 的最后一个参数 `initial_messages` 是一个可选的外部消息列表指针，设计初衷是让调用者传入预处理的对话消息（如 ContextManager 截断后的版本），使首轮 LLM 调用使用此列表而非 `conversation.messages()`。

## 现状

### 签名

```cpp
ToolCallLoopResult run(
    llm::Conversation& conversation,
    const std::vector<llm::ToolDefinition>& tools,
    const std::string& system_prompt,
    llm::StreamCallbacks callbacks,
    const ToolCallLoopConfig& config = {},
    const std::vector<llm::Message>* initial_messages = nullptr);  // ← 此处
```

### 使用方式

所有调用者：

| 调用者 | 传参 | 是否传 `initial_messages` |
|--------|------|--------------------------|
| `SerialProcessor::process()` | `&effective_messages`（局部变量地址） | ✅ 总是非空 |
| `SubAgent::executeTask()` | — | ❌ 默认 `nullptr` |

### 内部使用

```cpp
// 首轮
const auto& first_msgs = (initial_messages && !initial_messages->empty())
    ? *initial_messages : conversation.messages();

// 后续轮次（循环内）
response = client_.chat(conversation.messages(), tools, system_prompt, {});
```

## 发现的问题

### 1. 陈旧快照问题（Bug）

`initial_messages` 是在 `run()` 调用前从 `conversation` 拷贝的快照。如果 `run()` 内部修改了 `conversation`（例如预思考步骤 `use_thinking_step` 通过 `conversation.addAssistant()` 注入推理结果），首轮 LLM 调用看不到这些变化：

```
SerialProcessor::process()
    ├── buildEffectivePrompt()
    │     └── effective_messages = conversation.messages()  // 快照 A
    │
    └── loop.run(conversation, ..., &effective_messages)
          ├── thinking_step:
          │     conversation.addAssistant(推理结果)          // conversation → 快照 B
          │
          └── 首轮:
                first_msgs = *initial_messages               // 还是快照 A，不含推理结果
```

### 2. 首轮与后续轮次数据源不一致

| 轮次 | 数据源 | 内容 |
|------|--------|------|
| 首轮 | `*initial_messages` | 快照（调用 `run()` 之前的消息） |
| 第 2+ 轮 | `conversation.messages()` | 实时（含工具执行、反思注入的最新消息） |

如果 ContextManager 的 `compact()` 在 `buildEffectivePrompt()` 中修改了 `conversation`，则 `effective_messages` 和 `conversation.messages()` 内容可能不同，首轮和后续轮次看到不同的上下文视图，导致 LLM 行为不可预测。

### 3. `conversation.messages()` fallback 是死代码

```cpp
const auto& first_msgs = (initial_messages && !initial_messages->empty())
    ? *initial_messages : conversation.messages();
```

两个调用者各自固定走一个分支：

- `SerialProcessor` → `initial_messages` 永远非空 → 永远走 `*initial_messages`
- `SubAgent` → `initial_messages` 永远为 `nullptr` → 永远走 `conversation.messages()`

**没有同一个调用者会在不同条件下走不同分支**。`?:` 的条件判断从未在单一调用路径上发挥作用。

### 4. `const auto&` 跨栈帧引用风险

`first_msgs` 是一个 const ref 绑定到调用者的局部变量 `effective_messages`：

```cpp
// SerialProcessor.cpp
std::vector<llm::Message> effective_messages;   // 局部变量
auto result = loop.run(conversation, ..., &effective_messages);
                                                   ↑ 指针指向局部变量

// ToolCallLoop.cpp
const auto& first_msgs = *initial_messages;       // const ref 绑定到外部栈
```

当前虽然受同步调用保护生命周期安全（`run()` 同步返回后 `effective_messages` 才销毁），但这种设计：
- 限制了代码结构灵活性（无法安全重构为异步或延迟执行）
- 增加了阅读者的心智负担（需追踪跨函数栈的引用关系）

### 5. 设计冗余

`initial_messages` 存在的理由注释为"确保 token 截断策略真正生效"。但：

- ContextManager 的 `compact()` 已直接修改了 `conversation`
- 如果压缩已发生 → `effective_messages` 和 `conversation.messages()` 内容相同，快照多余
- 如果未压缩 → 本来就不需要截断
- 后续轮次直接使用 `conversation.messages()` 工作正常，说明首轮也可以用

## 建议修复

### 方案 A（推荐）：移除 `initial_messages` 参数

```cpp
// ToolCallLoop.cpp — 首轮统一使用 conversation.messages()
const auto& first_msgs = conversation.messages();
```

同时清理：

| 文件 | 修改 |
|------|------|
| `ToolCallLoop.h` | 移除 `initial_messages` 参数 |
| `ToolCallLoop.cpp` | 移除 `?:` 判断，直接使用 `conversation.messages()` |
| `IMessageProcessor.cpp` | `SerialProcessor::process()` 不再构建和传递 `effective_messages`；`loop.run()` 调用改为不传最后一个参数 |

### 方案 B：保留参数但修正快照问题

如果仍需保留外部注入消息的能力（例如未来子 Agent 需要），则至少修复：
1. 思考步骤后更新 `initial_messages` 或改用 `conversation.messages()`
2. 确保首轮和后续轮次的数据源一致

但考虑到当前没有实际需要外部注入消息的场景，方案 A 更简洁。

---

## 附带问题：首轮 LLM 调用的不必要计时

### 位置

`ToolCallLoop.cpp` 第 101-107 行（首轮 LLM 调用）以及循环内第 169-176 行（后续轮次 LLM 调用）、第 143-148 行（反思路径 LLM 调用）。

### 现状

```cpp
auto t1 = std::chrono::steady_clock::now();           // ← 计时开始
llm::LLMResponse response;
if (config.first_round_streaming || config.all_rounds_streaming)
    response = client_.chat(first_msgs, tools, system_prompt, callbacks);
else
    response = client_.chatNonStreaming(first_msgs, tools, system_prompt);
auto t2 = std::chrono::steady_clock::now();           // ← 计时结束
int round_ms = static_cast<int>(
    std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());
r.total_tokens_used += response.total_tokens;
r.input_tokens += response.prompt_tokens;
r.output_tokens += response.completion_tokens;
if (tracer_) tracer_->record("llm_response", response.total_tokens, round_ms);
```

### 问题

1. **`round_ms` 仅有 tracer 一个消费者** — 唯一用途是传给 `tracer_->record("llm_response", ...)`。如果没有 tracer（`tracer_ == nullptr`），计时完全浪费。

2. **`steady_clock::now()` 系统调用在热路径中** — 每次 LLM 往返（首轮 + N 轮循环 + 反思路径）都要额外做两次系统调用获取时间。虽然单次开销小（纳秒级），但在工具调用循环中属于不必要的噪声。

3. **计时代码割裂了业务逻辑** — `t1`/`t2`/`round_ms` 插入在 LLM 调用和 token 统计之间，使本可连续阅读的 token 累加逻辑被拦腰截断。

4. **循环内已经有两处计时**（`t3`/`t4` 用于 `tool_ms`，`t5` 用于 `round_ms`），加上首轮的 `t1`/`t2`，一个 `run()` 调用内部有 6 次 `steady_clock::now()` 调用。

### 建议

- **如果保留计时**：将 `t1`/`t2`/`round_ms` 移入 `if (tracer_)` 块内，无 tracer 时不产生系统调用开销。
- **如果倾向于删除**：计时逻辑内聚到 `LLMClient` 内部（客户端侧自己记录耗时并返回），或直接移除——`ToolCallLoopResult` 已有 token 统计指标，耗时可在调用方通过 `ToolCallLoop::run()` 整体计时覆盖。
