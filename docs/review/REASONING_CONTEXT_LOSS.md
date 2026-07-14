# 思考内容（reasoning_content）丢失问题

**发现日期**: 2026-07-14
**相关文件**: `src/llm/LLMClient.cpp`, `src/llm/Message.h`, `src/agent/ToolCallLoop.cpp`

---

## 问题描述

DeepSeek 支持返回独立的 `reasoning_content` 字段（思考/推理过程），与最终回复 `content` 分离。当前项目虽然能正确解析此字段，但在两个环节存在问题：

### 问题 1：未请求思考模式

`LLMClient::buildRequestBody()` 中**没有设置** `thinking: { type: "enabled" }` 和 `reasoning_effort`：

```cpp
// 当前 buildRequestBody 的结尾：
body["temperature"] = config_.temperature;
body["max_tokens"] = config_.max_tokens;
body["stream"] = stream;
return body;
// ✗ 缺少 body["thinking"] = {{"type", "enabled"}};
// ✗ 缺少 body["reasoning_effort"] = "high";
```

这意味着 DeepSeek 可能根本不返回 `reasoning_content`。

### 问题 2：思考内容存入对话时丢失

即使开启了思考模式，`ToolCallLoop` 构建 assistant 消息时只复制了 `content` 和 `tool_calls`：

```cpp
// ToolCallLoop.cpp:200-202
llm::Message assistant;
assistant.role = llm::MessageRole::Assistant;
assistant.content = response.content;          // ✅ 最终回复
assistant.tool_calls = response.tool_calls;    // ✅ 工具调用
// ✗ response.reasoning_content 被丢弃
```

原因：`Message` struct 没有 `reasoning_content` 字段。

```cpp
// Message.h
struct Message {
    MessageRole role;
    std::string content;
    std::vector<ToolCall> tool_calls;
    std::string tool_call_id;
    std::string name;
    bool preserved = false;
    // ✗ 没有 reasoning_content
};
```

### 完整链路

```
API 返回 reasoning_content
  → SSEParser ✅ 解析
  → StreamAccumulator ✅ 拼接
  → LLMResponse.reasoning_content ✅ 有值
  → StreamCallbacks::on_reasoning ✅ UI 可显示
  → Message（对话历史）✗ 丢失
  → 下一轮 API 请求 ✗ 不包含
```

## 影响

- 如果启用思考模式，LLM 的推理过程在后续轮次中不可用
- 对于需要长链推理的写作任务（角色弧光分析、剧情线规划），丢失思考内容可能导致一致性下降
- 当前实际没影响（因为思考模式根本没开启），但后续若启用则需修复

## 处理规则：分阶段决定 reasoning_content 的去留

核心判断依据：**当前循环是否还在工具调用过程中。**

### 🔁 阶段一：工具调用循环中 → 必须保留

当 `ToolCallLoop` 处于"LLM 返回 tool_calls → 执行工具 → 结果传回 LLM"的闭环中时，`reasoning_content` **必须保留**。

**原因**：DeepSeek API 要求，在一次工具调用的连续上下文中：
- 如果助手消息（`assistant` role）包含了 `tool_calls`
- 那么下一轮带上 `tool` 角色的执行结果返回时
- **必须同时提交该助手消息，且保留其 `reasoning_content`**
- 否则 API 会报参数错误，或模型无法将"工具执行结果"与"之前的推理意图"正确对齐

**工程实现**：`messages` 数组中完整保留带 `reasoning_content` 的助手消息。

```python
if msg.tool_calls:
    # ✅ 必须把带 reasoning_content 的完整消息加回上下文
    messages.append(msg)
    messages.append(tool_result_msg)
    continue  # 继续循环
```

### ✅ 阶段二：循环结束输出最终答案 → 丢弃

当 LLM **不再发起工具调用**，直接输出纯文本 `content` 作为最终答案时，循环结束。

**原因**：
- 思考内容的任务已完成，存回长期上下文会大量浪费 Token（思考通常比答案长数倍）
- 思考中的"自我怀疑"、"试错"等措辞可能污染下一轮全新对话

**工程实现**：存入历史时只保留 `content`，丢弃 `reasoning_content`。

```python
if not msg.tool_calls:
    # ✅ 最终答案只存 content，丢掉 reasoning_content
    final_history = {"role": "assistant", "content": msg.content}
    save_to_database(final_history)
    break  # 跳出循环
```

### 口诀

> **工具未停，推理必跟；任务已定，只留正文。**

---

## 修复方案

### Step 1：Message 增加 reasoning_content 字段

```cpp
struct Message {
    // ... 现有字段 ...
    std::string reasoning_content;  // 新增：DeepSeek thinking 模式的推理过程
};
```

同时更新 `Message::to_json` 和 `from_json`：

```cpp
// to_json 新增
if (!msg.reasoning_content.empty()) {
    j["reasoning_content"] = msg.reasoning_content;
}

// from_json 新增
msg.reasoning_content = j.value("reasoning_content", "");
```

### Step 2：ToolCallLoop 复制 reasoning_content

```cpp
// ToolCallLoop.cpp
llm::Message assistant;
assistant.role = llm::MessageRole::Assistant;
assistant.content = response.content;
assistant.reasoning_content = response.reasoning_content;  // 新增
assistant.tool_calls = response.tool_calls;
```

### Step 3：LLMClient 请求思考模式

在 `buildRequestBody()` 中根据配置决定是否开启：

```cpp
// LLMClient.cpp buildRequestBody
if (config_.enable_thinking) {
    body["thinking"] = {{"type", "enabled"}};
    body["reasoning_effort"] = config_.reasoning_effort;  // "high" 或 "max"
}
```

需要在 `ProviderConfig` 中增加配置项。

## 配置建议

思考模式是否开启应作为**可配置选项**，由用户决定：

```json
{
  "llm": {
    "enable_thinking": true,
    "reasoning_effort": "high"
  }
}
```

**思考模式开启后的 token 消耗会增加**（`reasoning_tokens` 会计入 `completion_tokens`），且思考内容本身也会占用上下文空间，因此默认建议关闭（当前行为），在需要复杂推理时由用户手动开启。

## 状态

- [ ] 待评估是否需要修复
