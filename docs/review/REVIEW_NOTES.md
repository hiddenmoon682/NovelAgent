# 待修复问题

> 创建时间: 2026-05-28
> 用途: 记录代码审查中发现的、尚未修复的问题
>
> 已修复的问题请记录在 `RESOLVED.md` 中。

---

> 以下为 2026-06-09 Phase 3.1-3.4 代码审查发现的问题。

---

## 1. [严重] ContextManager 的 system_prompt 永久覆盖用户设置的 system_prompt_

**文件**: `src/agent/Agent.cpp` — `runToolLoop()`

**问题**: 在 tool call 循环中，ContextManager 产出的上下文 system prompt（项目设定、章节信息）通过 `system_prompt_ = assembly.system_prompt` 直接覆盖了成员变量。用户通过 `setSystemPrompt()` 设置的「AI 人格提示词」（如"你是一个网文写作助手"）被永久替换，第一次调用后即丢失。

此外，ContextManager 的上下文 prompt 被存入成员变量后，会在后续不相关的对话轮次中继续使用（除非重新调用 `setSystemPrompt()` 恢复），造成上下文污染。

**根本原因**: 两类 system prompt 语义不同——「人格提示词」描述 AI 行为方式，「上下文提示词」描述项目信息——但当前把它们存入了同一个变量、且后者覆盖前者。

**建议**:
```cpp
// runToolLoop() 中改为局部变量组合，不修改成员
std::string effective_prompt = system_prompt_;
if (context_manager_) {
    auto assembly = context_manager_->assemble(...);
    if (!assembly.system_prompt.empty()) {
        effective_prompt = system_prompt_ + "\n\n" + assembly.system_prompt;
    }
}
// 所有 LLM 调用使用 effective_prompt
```
**影响范围**: 所有启用了 ContextManager 的场景，每次 LLM 调用的 system prompt 都可能不正确。

**严重度**: 🔴 严重

---

## 2. [严重] truncateMessages() token 减法公式与初始估值不一致

**文件**: `src/agent/ContextManager.cpp` — `truncateMessages()`

**问题**: 截断循环中逐条减去 token 时，只统计了 `content` 字段 + 硬编码的 `4`（结构开销），但 `total` 的初始值来自 `TokenCounter::countMessages()`，后者统计了完整的消息 token（含 role 标记、tool_calls 结构、name 字段等）。

两条公式的差值会累积：如果被移除的消息中有携带 `tool_calls` 的 assistant 消息（每次工具调用循环都有），`total` 下降量少于实际消耗量，可能导致截断不够、总 token 仍超出预算。

另一方面，`tool_calls` 的 `arguments` 可能很长（如 `write_chapter` 的全文内容），扣除 4 token 完全无法代表真实开销。

**建议**: 每次移除后重新计算，避免手工同步两套公式：
```cpp
while (!result.empty() && llm::TokenCounter::countMessages(result) > budget) {
    result.erase(result.begin());
    ++truncated_count;
}
```
如果担心性能，可以在循环外层加一个最大迭代次数的保护。

**影响范围**: tool call 循环场景（assistant 消息含 tool_calls 时），截断准确性下降。

**严重度**: 🔴 严重

---

## 3. [严重] buildSystemPrompt() 对不存在的章节返回空字符串

**文件**: `src/agent/ContextManager.cpp` — `buildSystemPrompt()`

**问题**: 当传入一个不存在或无效的 `chapter_id` 时，`PromptContextBuilder::buildForChapter()` 返回 `std::nullopt`，当前代码直接 `return {}`（空字符串）。对比之下，不传 `chapter_id` 的路径会返回最小化的项目概述（标题、logline、主题）。

这意味着：传错误章节 ID 比不传章节 ID 更糟糕——前者 LLM 完全没有项目上下文，后者至少能知道项目名称和主题。

**建议**: `buildForChapter` 失败时 fallback 到无章节版本：
```cpp
if (!ctx) {
    spdlog::warn("[ContextManager] 无法为章节 '{}' 构建上下文，回退到项目概述", chapter_id);
    return buildSystemPrompt(project);  // 无章节版本，至少返回项目基本信息
}
```

**影响范围**: 当 LLM 调用工具时传入了已删除或拼写错误的章节 ID。

**严重度**: 🔴 严重

---

## 4. [中等] truncateMessages() 在 budget ≤ 0 时返回全部消息

**文件**: `src/agent/ContextManager.cpp` — `truncateMessages()`

**问题**: 当 `budget <= 0` 时（极端情况：context_window 极小，或 system_prompt 已用尽全部预算），当前代码直接返回全部消息列表。这些消息无法被 LLM 容纳，应返回空列表。

```cpp
// 当前代码
if (messages.empty() || budget <= 0) {
    return messages;  // ← 预算为 0 时不应返回消息
}
```

**建议**:
```cpp
if (messages.empty()) return messages;
if (budget <= 0) {
    truncated_count = static_cast<int>(messages.size());
    return {};
}
```

**影响范围**: 极端小窗口场景（如用 context_window=1024 的廉价模型做简单查询）。

**严重度**: 🟡 中等

---

## 5. [中等] ChapterTools 持有 Project& 裸引用 — 悬空引用风险

**文件**: `src/agent/tools/ChapterTools.h` — 全部 5 个工具类

**问题**: 所有 Chapter 工具在构造时接收 `Project&` 引用并存储为成员。如果 Project 对象被销毁、移动或用户切换项目，但 ToolRegistry 中的工具实例仍然存活（ToolRegistry 持有 `unique_ptr<BuiltInTool>`），访问 `project_` 就是 use-after-free。

当前使用场景中 tool 和 project 生命周期一致，不会触发，但这是隐式约定而非强制约束。随着 Phase 3.5 多 Agent 并行编排的引入，sub-agent 可能持有对已释放 Project 的引用。

**建议**:
- **短期**: 在 `BuiltInTool` 文档中明确标出生命周期约束："工具实例的生命周期不得长于构造时传入的外部引用"
- **长期**: 改为 `std::shared_ptr<Project>`，或让工具的 `execute()` 方法接收 project 引用参数而非在构造时持有

**影响范围**: 未来多 Agent 场景、项目切换场景。当前无实际触发路径。

**严重度**: 🟡 中等

---

## 6. [中等] CreateChapterTool 部分保存与全量保存的不一致风险

**文件**: `src/agent/tools/ChapterTools.cpp` — `CreateChapterTool::execute()`

**问题**: `CreateChapterTool` 只调用 `ProjectIO::saveJsonFile()` 保存 `outline.json`，而非 `ProjectIO::save()` 保存全部 6 个 JSON 文件。这是一个刻意的性能优化（避免覆盖用户对 characters/settings 等文件的并行修改）。

但存在隐式风险：如果内存中 `characters.json`、`settings.json` 等有尚未落盘的修改，后续某处调用 `ProjectIO::save()` 时，会用内存中的旧状态覆盖 `outline.json`，导致新创建的章节数据丢失。当前代码中不会有"未落盘修改"（工具都直接操作 project 内存 + 文件），但这是脆弱的隐式保证。

**建议**: 
- 方案 A: `ProjectIO` 新增 `saveOutline()` 方法，内部只在 `outline.json` 上加文件锁，与其他 JSON 文件独立
- 方案 B: 始终调用 `ProjectIO::save()` 全量保存（当前的瓶颈在 I/O 上是可忽略的）

**影响范围**: 如果未来某处代码对 project 做内存修改后不立即落盘，再调用 CreateChapter → 后续 save 时可能丢数据。

**严重度**: 🟡 中等

---

## 7. [中等] 工具调用结果无大小限制，可能导致单条消息超出 token 预算

**文件**: `src/agent/Agent.cpp` — `executeToolCallsAndAppend()`

**问题**: 工具执行结果直接 `result.dump()` 后完整加入对话历史。如果 `read_chapter` 读取了一个 10000+ 字的章节，或 `list_chapters` 有 200 个章节，单条 tool result 消息可能就有数万字符。即使 ContextManager 做截断，单条巨大消息也可能导致内存浪费和 LLM 处理困难。

另外，LLM 在后续轮次中不需要看到完整章节原文——它需要的是"我读了第 X 章，内容是……"的确认信息。

**建议**: 在结果加入对话前做截断：
```cpp
constexpr size_t kMaxToolResultChars = 4000;
std::string result_str = result.dump();
if (result_str.size() > kMaxResultChars) {
    result_str = result_str.substr(0, kMaxResultChars) + "\n...(已截断)";
}
conversation_.addToolResult(tc.id, result_str);
```

**影响范围**: 章节读写场景，尤其是长章节的 `read_chapter` 调用。

**严重度**: 🟡 中等

---

## 8. [轻微] 空的 try/catch 块 — 捕获后立即重新抛出

**文件**: `src/agent/Agent.cpp:51-56`

**问题**: `processUserMessage()` 中对 `runToolLoop()` 的调用被 `try { ... } catch (...) { throw; }` 包裹，这个 try/catch 块不做任何日志或清理，直接重新抛出，等价于不存在。

**建议**: 删除该 try/catch 块，或在 catch 中做有意义的操作（如 spdlog::error 记录异常信息）。

**严重度**: 🟢 轻微

---

## 9. [轻微] ListChaptersTool 描述与实际行为不一致

**文件**: `src/agent/tools/ChapterTools.h:67`

**问题**: 工具描述声称返回"ID、标题、顺序和字数"，但实现中不包含 `word_count` 字段。这是 CHANGELOG 中记录的刻意优化（避免 100 章时 100 次磁盘 I/O），但工具描述未同步更新。LLM 读取描述后可能会在回复中声称"第 X 章有 Y 字"，但实际上它没有这个数据。

**建议**: 更新 `description()` 为"列出当前项目所有章节的 ID、标题、顺序和摘要"，或添加不读文件的估算字数（如基于 title/synopsis 长度估算）。

**严重度**: 🟢 轻微

---

## 10. [轻微] SchemaUtils::object() 全局硬编码 additionalProperties: false

**文件**: `src/utils/SchemaUtils.h:41`

**问题**: `additionalProperties: false` 作为安全默认是好的，但不是所有 LLM function calling 实现都严格遵守——某些模型偶尔在参数中附加额外字段。如果遇到这种情况，API 端参数校验可能拒绝请求。

**建议**: 当前不做修改（保留此安全默认），但在注释中记录此设计决策。如果后续遇到兼容性问题，可通过 `SchemaUtils::object(..., bool allowExtra = false)` 添加可选参数。

**严重度**: 🟢 轻微

---

## 11. [轻微] ContextAssembly::total_tokens 精度说明缺失

**文件**: `src/agent/ContextManager.h:23`

**问题**: `total_tokens` 字段注释为"实际占用的 token 数"，但实际上 `TokenCounter` 是启发式估算（中文 × 0.75 + 英文 × 1.3），不是精确值。所有消费此字段的代码都应意识到这是估值。

**建议**: 更新注释为 `"实际占用的 token 数（估算值，非精确计数）"`。

**严重度**: 🟢 轻微

---

## 附录：审查覆盖范围

| 模块 | 文件 | 审查内容 |
|------|------|---------|
| Step 3.1 — ToolRegistry | `ToolRegistry.h/.cpp`, `BuiltInTool.h`, `SchemaUtils.h` | 双注册方式、错误处理、所有权管理 |
| Step 3.2 — Agent | `Agent.h/.cpp` | 核心循环、tool call 编排、对话历史管理 |
| Step 3.3 — ContextManager | `ContextManager.h/.cpp` | 预算计算、消息截断、system prompt 构建 |
| Step 3.4 — ChapterTools | `ChapterTools.h/.cpp` | 5 个工具实现、文件 I/O、错误处理 |
| 测试 | 4 个 test 文件 | Mock HTTP 服务器、集成测试、边界条件 |

审查日期: 2026-06-09 | 发现: 3 🔴 + 4 🟡 + 4 🟢

---

## 附录：添加新条目

发现新的问题后，按以下格式追加：

```markdown
## N. 标题

**文件**: `相关文件路径`

**问题**: 描述...

**建议**: 如何修复...
```
