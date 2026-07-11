# maybeAutoCompact 章节切换自动压缩设计问题

> 记录日期：2026-07-11

## 问题：maybeAutoCompact 的章节切换触发逻辑不合理

`Agent::maybeAutoCompact()`（`Agent.cpp:195-235`）在 LLM 响应返回后，通过扫描 `tool_calls` 中的 `create_chapter`/`read_chapter` 来检测章节切换，并自动触发 `compactConversation()`。这个设计存在多个问题。

## 分析

**1. 触发时机错误**

触发条件是 LLM 刚返回的 `tool_calls` 中有 `read_chapter`/`create_chapter`——这意味着**当前这轮 LLM 响应已经拿到了新章节内容**。然后立即压缩旧章节的对话，上一轮用户对旧章节的指令也被压缩了，用户根本没机会看到 LLM 在旧章节上做了什么。

**2. 读旧章参考触发意外切换**

```
当前在写 ch-005
LLM 为了参考前文调了 read_chapter("ch-001")
  → maybeAutoCompact 检测到 new_chapter_id = "ch-001"
  → current_chapter_id_ = "ch-001"
  → 下一轮：system prompt 变成了 ch-001 的上下文
```

LLM 只是想参考一下，结果当前上下文被切换了。

**3. 自动触发不可撤销**

`/compact` 是用户主动选择，可以接受或拒绝。章节切换时自动触发让用户没有选择权。

**4. 与 shouldAutoCompact 功能重叠**

`ContextManager::shouldAutoCompact()`（`ContextManager.cpp:193-195`）已基于 token 用量百分比提供自动压缩。章节切换时再附带一个 compact 是多此一举——如果 token 窗口没满，不需要压缩；如果满了，`shouldAutoCompact` 会在步骤 4（`Agent.cpp:332-340`）触发。

**5. 可能破坏正在进行的多章节创作流**

LLM 在同一个响应中先后调用 `read_chapter("ch-001")` 和 `read_chapter("ch-002")`（对比两章），`maybeAutoCompact` 被触发，旧章节对话被压缩，但用户实际只需要一个简单的对比。

**6. 章节追踪通过偷看 tool_calls 推断**

`chapter_id` 不是 LLM 或用户主动声明的，而是 Agent 在 LLM 返回响应后"偷看"其 `tool_calls` 参数推断的。这导致滞后一轮的问题：第 N 轮 LLM 创建了 ch-005，第 N+1 轮 `buildSystemPrompt` 才输出 ch-005 的上下文。

## 位置

| 位置 | 内容 |
|------|------|
| `Agent.cpp:195-235` | `maybeAutoCompact()` 函数体 |
| `Agent.cpp:375` | 调用点（步骤 7） |
| `Agent.h:146` | `last_chapter_id_` 成员 |
| `Agent.h:155` | `maybeAutoCompact` 声明 |
| `Agent.cpp:332-340` | 另一个自动压缩入口（基于 token 用量，合理） |

## 相关调用链

```
processUserMessage()
  ├─ 步骤 4 (Agent.cpp:332): shouldAutoCompact() → compactConversation()
  │    基于 token 用量触发，合理
  │
  ├─ 步骤 6 (Agent.cpp:353): processor_->process() → LLM 调用
  │
  └─ 步骤 7 (Agent.cpp:375): maybeAutoCompact(response)
       基于 tool_calls 偷看章节切换，触发：
         1. compactConversation("切换到新章节 xxx")
         2. last_chapter_id_ = new_chapter_id
         3. context_manager_->setCurrentChapter(new_chapter_id)
            → 影响下一轮 assemble() 中 buildSystemPrompt 的章节上下文
```

## 建议

1. **删除 `maybeAutoCompact()` 的章节切换触发逻辑**，只保留步骤 4 基于 `shouldAutoCompact()` 的自动压缩
2. **`last_chapter_id_` 的追踪**：如果只是给 `saveSessionState`/`loadSessionState` 用，保留其更新逻辑即可，不需要触发 compact
3. **`current_chapter_id_` 的更新**：应该由 LLM 完成章节编写后自然产生的结果来决定，而不是通过偷看 tool_calls。可选方案：
   - 在 `create_chapter` 工具的执行结果中回传 chapter_id，Agent 据此更新
   - 增加 `/chapter` REPL 命令让用户手动指定
   - 首次进入项目时，取第一章或最近写过的一章作为默认值

## 执行记录

> 2026-07-11：已实施全索引模式（选项 B）。

**删除内容：**
- `maybeAutoCompact()` 整个函数（`Agent.cpp`）
- `last_chapter_id_` 成员（`Agent.h`）
- `current_chapter_id_` + `setCurrentChapter` + `lightweight_mode_`（`ContextManager.h`）
- `SessionMeta::last_chapter_id`（`SessionPersistence.h`）
- `buildSystemPrompt` 的模式 2/3（章节上下文自动注入）

**新增：**
- `GetLatestChapterTool`（`get_latest_chapter` 工具）— LLM 按需查询最新章节
- `buildSystemPrompt` 始终输出项目概要 + `renderToolUseInstructions()`
- `renderToolUseInstructions` 更新：加入 `get_latest_chapter` 条目和写作流程步骤 1

## 残余风险

1. `Agent::setCurrentChapter()`（`Agent.cpp:118-120`）只更新 `last_chapter_id_` 但不更新 `context_manager_->current_chapter_id_`，与 `maybeAutoCompact` 的行为不一致。该方法当前仅被 `loadSessionState` 调用。
2. `ContextManager::current_chapter_id_` 在 `assemble()` 步骤 0 被用于 `buildSystemPrompt`，初始为空字符串——首次写作没有章节级上下文。
3. 向量检索的去重循环（`ContextManager.cpp:368-375`）也依赖 `current_chapter_id_`，存在同样的滞后问题。

---

## 删除方案

### 目标

删除 `maybeAutoCompact()` 函数及其调用点，移除 `last_chapter_id_` 的 tool_call 偷看式追踪。紧凑只由步骤 4 的 `shouldAutoCompact()`（基于 token 用量）触发。

### 影响分析

| 组件 | 影响 |
|------|------|
| `Agent::processUserMessage()` 步骤 7 | 不再调用 `maybeAutoCompact`，其余逻辑不变 |
| `Agent::compactConversation()` | 保留，/compact 命令和步骤 4 仍会调用 |
| `Agent::saveSessionState()` | 仍需要 `last_chapter_id_`（写 session_meta 用），保留字段 |
| `Agent::loadSessionState()` | 恢复 `last_chapter_id_` 后仍需 `setCurrentChapter`，保留 |
| `Agent::setCurrentChapter()` | 保留，/compact 命令和步骤 4 仍会调用（可能退化） |
| `ContextManager::current_chapter_id_` | `buildSystemPrompt` 仍依赖它确定章节上下文，后续需改为更合理的更新机制 |

### 删除步骤

**1. `Agent.cpp` — 删除 `maybeAutoCompact` 函数体**

删除第 195-235 行整段：

```cpp
void Agent::maybeAutoCompact(const llm::LLMResponse& response) {
    ...
}
```

**2. `Agent.cpp:375` — 删除调用点**

```cpp
// before:
        // ── 章节边界检测 ──
        maybeAutoCompact(result.raw_response);

        // ── 步骤 7.5: 会话增量保存 ──

// after:
        // ── 步骤 7.5: 会话增量保存 ──
```

**3. `Agent.h` — 删除声明及相关注释**

- 删除 `last_chapter_id_` 成员（第 146 行）**或保留**（需确认 `saveSessionState`/`loadSessionState` 是否需要它）
- 删除 `maybeAutoCompact` 声明（第 155 行）及上方注释块（第 147-154 行）
- 删除 `# 章节边界检测 ──` 注释

#### 关于 `last_chapter_id_` 的处理

`last_chapter_id_` 在以下位置被使用：

| 位置 | 用途 | 删除后影响 |
|------|------|-----------|
| `Agent.cpp:178` — `saveSessionState()` | 传入 `last_chapter_id_` 写 session_meta | 恢复会话时丢失章节上下文 |
| `Agent.cpp:188` — `loadSessionState()` | 从 session_meta 恢复 `last_chapter_id_` | 同上 |
| `Agent.cpp:191` — `loadSessionState()` | 恢复后调 `setCurrentChapter` 同步到 ContextManager | 同上 |
| `Agent.cpp:119` — `setCurrentChapter()` | 写入 `last_chapter_id_` | 仅被 `loadSessionState` 调用 |

**结论：建议保留 `last_chapter_id_` + `setCurrentChapter()` + `saveSessionState`/`loadSessionState` 的章节 ID 传递，** 因为会话持久化需要记录当前在写哪个章节。删除的只是**用 tool_calls 偷看章节切换并自动 compact** 这一部分。

### 删除后的流程

```
processUserMessage()
  ├─ 步骤 4: shouldAutoCompact() → compactConversation()
  │    基于 token 用量触发，窗口满了才压缩
  │
  ├─ 步骤 6: processor_->process() → LLM 调用
  │
  └─ 步骤 7.5: saveSessionState()
        last_chapter_id_ 从会话文件恢复，不受影响
```

### 不删除的内容（保留不动）

| 内容 | 理由 |
|------|------|
| `shouldAutoCompact()` / `setAutoCompact()` | token 用量自动压缩机制，合理 |
| `compactConversation()` | /compact 命令和步骤 4 仍需调用 |
| `last_chapter_id_` 成员 | 会话持久化需要 |
| `saveSessionState` / `loadSessionState` | 章节 ID 持久化需要 |
| `ContextManager::setCurrentChapter()` | 由 `loadSessionState` 调用 |
| `ReplHandler::autoSaveOnError()` | 崩溃自动保存，无关 |

### 后续可讨论的问题

1. `current_chapter_id_` 的初始值：首次进入项目时，应取第一章或最近写过的一章作为默认值，避免 `buildSystemPrompt` 输出空概要
2. `current_chapter_id_` 的更新机制：是否由 `create_chapter` 工具执行结果回传 chapter_id？是否增加 `/chapter` REPL 命令？
3. 向量检索去重（`ContextManager.cpp:368-375`）的空循环修复
