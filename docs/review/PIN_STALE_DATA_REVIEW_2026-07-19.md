# Pin 数据过时问题审查

## 背景

`ToolPipeline` 的自动 pin 机制（`kSettingTools`）在 `create_*` 工具执行时将其 `tool_result` 标记为 `preserved=true`，确保这些「设定类」消息在对话压缩时优先保留。但**当 LLM 后续调用 `update_*` 修改同一实体时，旧的 pinned 消息不会被自动 unpin**，导致对话上下文中同时存在新旧两个版本的信息。

## 问题场景

```
步骤 1: LLM 调用 create_character("Alice", personality="勇敢")
         → tool_result 被 pin（preserved=true）
         → 对话中永久存在："Alice 性格：勇敢"

步骤 2: LLM 调用 update_character("Alice", personality="狡诈")
         → tool_result 也被 pin（update_* 同样在 kSettingTools 中）
         → 旧的 create_character 结果仍处于 pinned 状态

结果: 对话上下文中同时存在两条 pinned 消息：
      [pinned] 性格：勇敢  ← 过时数据，但仍优先保留
      [pinned] 性格：狡诈  ← 最新数据
```

## 风险

- LLM 可能基于过时的 pinned 信息做出错误决策（如按旧性格写章节）
- 虽然 LLM 通常倾向遵循最新指令/信息，但这是模型行为依赖，不是系统保障
- 真正的权威数据存储在 `Project`（`novel.json`）中，但 LLM 在生成时并不自动 re-read

## 现有缓解措施

1. **`update_*` 结果也被 pin** — 最新版本的信息同样被保留在上下文中
2. **`editMessage()` 时重置 `preserved = false`** — 但仅限 `/edit` 命令，不适用于工具更新场景
3. **没有自动 unpin 旧版本的机制**

## 可能修复方向

### 方案 A: ConversationDiff 增加 unpin 能力

给 `ConversationDiff` 增加 `std::vector<size_t> unpin_global_indices` 字段，`apply()` 时一并处理。ToolPipeline 需要某种方式知道「哪个实体被更新了，对应的旧消息在哪」。

**问题**：ToolPipeline 当前不持有 Conversation 引用（这正是 `ConversationDiff` 的设计目标——解耦），无法遍历对话查找旧消息。要查找旧消息，需要 ToolPipeline 有读 Conversation 的能力，回到耦合。

### 方案 B: 在 Agent/ToolCallLoop 层处理

不修改 ToolPipeline 或 ConversationDiff，而是由 ToolCallLoop 在 `apply(diff)` 之后扫描对话，发现同实体的前一版本并 unpin。

**问题**：需要定义「同实体」的识别逻辑（通过 tool_call 返回值中的 entity_id），并且 ToolCallLoop 需要知道哪些工具是更新操作。

### 方案 C: 不自动 unpin，依赖 re-read

LLM 在生成章节前，通过 `get_character` 工具读取最新数据，不依赖对话中的历史 tool_result。pin 只保证「历史操作记录」不被压缩，不保证信息最新。

**问题**：LLM 需要主动调用 re-read，否则仍可能被旧信息误导。

## 当前结论

尚未实施修复。如果观察到 LLM 被过时 pinned 信息误导，优先考虑方案 A（先给 `ConversationDiff` 加 `unpin_global_indices`），因为改动范围最小、不影响现有解耦。

---

Reviewed: 2026-07-19
Related: [[ToolPipeline auto-pin in kSettingTools]], [[ConversationDiff design rationale]]
