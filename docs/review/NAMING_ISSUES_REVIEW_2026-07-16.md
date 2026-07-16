# 命名问题审查 — 2026-07-16

## 问题：`effective_prompt` 命名歧义

### 现状

在 `SerialProcessor::process()`、`ParallelProcessor::process()` 和 `Agent::execute()` 中，存在以下变量：

```cpp
// IMessageProcessor.cpp — SerialProcessor::process()
std::vector<llm::Message> effective_messages;     // ← 对话消息（user/assistant/tool）
auto effective_prompt = buildEffectivePrompt(...); // ← 实际上是系统提示词

// IMessageProcessor.cpp — ParallelProcessor::process()
std::string effective_prompt = system_prompt_;    // ← 同

// Agent.cpp — Agent::execute()
std::string effective_prompt = system_prompt_;    // ← 同
```

### 问题描述

`effective_prompt` 这个变量名存在严重歧义：

1. **"prompt" 含义过宽** — 在 LLM 上下文中，"prompt" 可以指 system prompt、user prompt 或完整的 messages 数组。`effective_messages` 和 `effective_prompt` 并列使用时，读者容易误以为前者是"消息"、后者是"完整的提示内容"。

2. **实际含义是 system prompt** — 该变量始终存储的是经过 ContextManager 动态拼装后的**系统提示词**，通过 `buildEffectivePrompt()` 构建后作为 `system_prompt` 参数传给 `ToolCallLoop::run()` 和 `LLMClient::chat()`：
   ```cpp
   auto result = loop.run(conversation, tools, effective_prompt, ...);
   //                                           ↑ 传给 system_prompt 参数
   ```

3. **与 `effective_messages` 形成误导性对仗** — `effective_messages` 和 `effective_prompt` 并列时，语义应该是"有效消息"和"有效提示词"，但实际上前者是对话消息（不包含 system），后者是系统提示词（不包含对话消息），两者是**互补**关系而非并列关系。

### 建议修复

将 `effective_prompt` 重命名为 `effective_system_prompt`，明确其角色是"有效的系统提示词"：

| 文件 | 位置 | 原名 | 建议改后名 |
|------|------|------|-----------|
| `src/agent/IMessageProcessor.cpp` | `SerialProcessor::process()` | `effective_prompt` | `effective_system_prompt` |
| `src/agent/IMessageProcessor.cpp` | `ParallelProcessor::process()` | `effective_prompt` | `effective_system_prompt` |
| `src/agent/Agent.cpp` | `Agent::execute()` | `effective_prompt` | `effective_system_prompt` |

### 关联问题

此命名问题与 **系统提示词所有权问题**（见 `SYSTEM_PROMPT_OWNERSHIP_REVIEW_2026-07-16.md`）相关——当系统提示词统一由 `Conversation` 管理后，这里的 `effective_system_prompt` 的来源应改为 `conversation.systemPrompt()`，而非从 `SerialProcessor::system_prompt_` 读取。
