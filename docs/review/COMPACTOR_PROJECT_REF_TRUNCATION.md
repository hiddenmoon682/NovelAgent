# Compaction/上下文管理 相关设计问题记录

> 记录日期：2026-07-10
> 最后更新：2026-07-11
>
> 已解决的问题（6/12）：
> - ✅ 问题一：buildProjectRef 截断多余 → 整体删除
> - ✅ 延伸问题：buildProjectRef 放在 Compactor 内部不合理 → 随函数删除
> - ✅ 延伸问题：章节切换自动 compact → maybeAutoCompact 删除
> - ✅ 延伸问题：current_chapter_id_ 追踪断裂 → 全套机制移除，全索引模式
> - ✅ 重构方案：工具替代自动注入 → 已实施（get_chapter_context + get_latest_chapter）
> - ✅ 去重逻辑半成品（问题四第 4 点）→ covered_ids 随 current_chapter_id_ 移除
>
> 未解决的问题（6/12）：
> - ⚠️ 延伸问题：assemble() 自动向量检索不可控（去重已修，自动检索仍在）
> - ❌ 问题：压缩摘要注入到 system prompt 而非对话中
> - ❌ 问题：assemble() 步骤 4 告警依赖过时数据
> - ❌ 问题：truncateMessages 安全网几乎不触发
> - ❌ 问题十：用强迫压缩替代截断作为安全网
> - ❌ 问题十一：assemble() 步骤 7 状态缓存反馈循环

## 问题一：buildProjectRef 的 1200 字节截断多余（✅ 已修复）

`src/agent/Compactor.cpp:44-61` 的 `buildProjectRef` 函数在调用 `PromptContextBuilder::buildForChapter` 构建完整项目上下文后，用 1200 字节硬限制截断输出。这个截断是多余的。

## 分析

**1. 输出已是有界的**

`buildForChapter` 的默认上限：6 条剧情线、8 个角色、8 个设定、6 条规则（`PromptContextBuilder.h:46-49`），渲染后纯文本通常在 2-5 KB，量级合理。

**2. 调用方已有双重防护**

`Compactor::compact()` 第 108-123 行的 `model_context_limit_` 检查会估算 system prompt + 对话文本的总 token 数，超出时自动截断对话侧。project ref 的增加会被这个机制自然容纳，不会导致 API 400。

**3. 截断反而有害**

1200 字节约等于 400 个中文字符，在 8 个角色的 context 下几乎必然触发截断，而这恰恰会破坏"为压缩器提供角色/设定参考"这一设计目的。

**4. 计算浪费**

`buildForChapter` 做了完整的筛选、过滤、渲染工作，结果在最后一步被砍掉大部分——白做了。

## 位置

- **文件**：`src/agent/Compactor.cpp`
- **函数**：`buildProjectRef`（匿名命名空间内，第 44-61 行）
- **调用方**：`Compactor::compact()` 第 124 行
- **截断逻辑**：第 53-58 行

## 建议

1. 删除第 53-58 行的截断逻辑，`model_context_limit_` 检查才是真正的安全网。
2. 如果保留 project ref，应该让 `buildProjectRef` 显式传入适合 compaction 场景的 limits（而非走 `write_chapter` 的默认配置），比如 `max_characters = 4`、`max_plot_threads = 3`。

### 执行记录

> 2026-07-11：`buildProjectRef` 整体删除。该函数在 compaction 场景下注入当前章节的上下文帮助有限（被压缩的对话正文已包含角色信息），且 1200 字节截断破坏设计目的。Compactor 不再依赖 `PromptContextBuilder`。删除范围：`Compactor.cpp` 中整个 `buildProjectRef` 函数及其调用点，`Compactor.h` 中 `chapter_id`/`project` 参数，`ContextManager.cpp` 中相关传参，`test_compactor.cpp` 中 6 处调用参数清理。

---

## 延伸问题：buildProjectRef 放在 Compactor 内部不合理（✅ 已修复）

### 问题

`buildProjectRef` 在 `Compactor::compact()` 内部被调用，由 compaction 自己去获取 project 上下文。这个设计站不住脚。

### 分析

**1. 被压缩的对话已包含小说正文**

历史消息中用户和助手的对话已经写出的章节内容天然带着人物和情节。如果正文写了"张三推开门"，压缩 LLM 不需要 project ref 也知道张三存在。额外灌入角色档案并非必需。

**2. project ref 的 scope 不匹配**

`buildProjectRef` 取的是"当前章节"的上下文。但被压缩的对话可能覆盖了多个章节的历史——只给当前章节的信息，对跨度更大的历史摘要帮助有限。

**3. 与 system prompt 的角色错位**

Compaction 的 system prompt 要求"双层摘要：情节事实 + 风格参考"。追加一段"当前项目设定"后，压缩器到底需要在摘要里体现角色档案吗？不需要——摘要要的是情节事实，不是设定列表。

**4. 关注点混淆**

Compaction 的核心职责是"压缩对话"，它不应该自己决定要取什么 project 上下文。如果调用方认为压缩时需要 project ref，应该由调用方传入，而不是 compaction 内部自己去获取。

### 建议

如果确实需要压缩时带上项目参考，应该由 `CompactRequest` 或类似结构体携带预处理好的上下文文本，`compact()` 只负责拼接和调用 LLM，不负责调用 `PromptContextBuilder`。

---

## 延伸问题：章节切换时自动触发 compact 不合理（✅ 已修复）

### 问题

`Agent::maybeAutoCompact()`（`Agent.cpp:191-231`）在检测到 LLM 切换章节时自动调用 `compactConversation()`。这个设计站不住脚。

### 分析

**1. 章节切换不意味着对话内容过时**

实际创作场景中，用户可能刚写完第一章、要求"改个结尾"；或者在第二章写到一半说"按第一章的风格重写"。这时第一章的对话恰恰是核心上下文，不该被压缩。

**2. 触发时机和实际效果脱节**

`maybeAutoCompact` 的触发条件是 LLM 刚返回的 `tool_calls` 中有 `read_chapter`/`create_chapter`——这意味着**当前这轮对话的 LLM 响应已经拿到了新章节内容**。然后立即压缩掉旧章节的对话...那上一轮用户对旧章节的指令也被压缩了。

**3. 自动触发不可撤销**

`/compact` 是用户主动选择，可以选择接受或不接受。章节切换时自动触发让用户没有选择权。

**4. 已有更合理的自动触发机制**

`ContextManager` 已有 `shouldAutoCompact()`，基于 token 用量百分比（默认 70%）判断。这才是合理的自动触发条件——跟章节无关，只跟窗口满了有关。章节切换再附带一个 compact，是多此一举。

**5. 可能破坏正在进行的多章节创作流**

例如 LLM 在同一个响应中先后调用了 `read_chapter("ch-001")` 和 `read_chapter("ch-002")`（对比两章内容）。这时 `maybeAutoCompact` 被触发，旧章节的对话被压缩，但用户实际只需要一个简单的对比。

### 建议

1. 删除 `maybeAutoCompact()` 中的章节切换触发逻辑，只保留基于 `shouldAutoCompact()` 的自动压缩
2. 如果需要保留章节切换时的压缩提示，改为输出一条建议消息（"切换了章节，可考虑 /compact"），由用户决定是否执行

---

## 延伸问题：assemble() 中的自动向量检索不可控（⚠️ 部分修复）

### 问题

`ContextManager::assemble()` 第 329-404 行在每次 LLM 请求前自动执行向量检索，用最近 3 条用户消息做查询语义搜索，结果硬塞入 system prompt。这种方式存在多个隐患。

### 分析

**1. 查询文本质量不可控**

语义检索的效果完全取决于查询文本的质量。但用户消息可能是"继续""好的""改一下"这类短指令——embedded 后会匹配到一堆无关联的"继续"。A10 修复（拼 3 条消息）缓解了单条短消息的问题，但本质没变：一个跟内容无关的对话回合就会让整个查询失效。

**2. 注入而非按需，LLM 无法拒绝**

检索到的片段被直接追加到 `result.system_prompt` 末尾。对 LLM 来说，system prompt 等价于"必须参考的事实"，不能忽略。而实际在写第八章时，注入第三章的片段极大概率是干扰——既浪费 token，又可能在注意力机制中产生误导。

**3. 已有更合理的替代路径**

`search_memory` 工具（SearchMemoryTools.cpp）已经是完善的对等实现——LLM 自己构造 query、自己决定何时搜索、结果在对话消息中可被后续上下文自然遗忘。`assemble()` 的自动注入与 `search_memory` 功能完全重叠。

**4. 去重逻辑是半成品（✅ 已清理）**

第 370-373 行的"相邻章节去重"：
covered_ids.insert(current_chapter_id_);
for (const auto& ch : project_->outline.chapters) {
    if (ch.id == current_chapter_id_) continue;
    // 相邻章节也标记为已覆盖（缩减范围而非全包含）
}
循环体是空的——注释说标记相邻章节，实际只有当前章节被加入了 `covered_ids`。这个去重只跳过了与当前章节 ID 完全相同的结果，相邻章节从未被排除。

> 2026-07-11 全索引模式已移除 `current_chapter_id_` 和 `covered_ids`，此去重逻辑不再存在。

**5. 每轮都有 API 开销**

每轮 LLM 请求前都做一次 embeddings API 调用（POST /v1/embeddings）+ O(n) 暴力遍历全库。在长会话中，这个开销是累积的。

### 建议

1. 去掉 `assemble()` 中的自动向量检索，让 LLM 完全通过 `search_memory` 工具按需搜索
2. 如果保留自动注入，至少加一个相似度阈值（如低于 0.6 不注入），避免噪声进入 system prompt
3. 修复第 370-373 行的空循环去重，或者直接删除这段无效代码（✅ 已执行）

> 执行记录：2026-07-11 — 去重逻辑随 `current_chapter_id_` 移除而清理（第 3 点已修）。建议 1、2 未实施。

---

## 延伸问题：current_chapter_id_ 的追踪机制存在多处断裂（✅ 已修复）

### 问题

`current_chapter_id_` 是 `buildSystemPrompt` 决定"输出哪个章节的上下文"的唯一依据。但这个 ID 的获取、传递和更新方式存在多个设计缺陷。

### 分析

**1. chapter_id 是事后推断，不是事先声明**

`chapter_id` 不是用户或 LLM 主动声明的，而是 Agent 在 LLM 返回响应后"偷看"其 `tool_calls` 参数推断出来的。这意味着：

第 N 轮：
  LLM 调用 read_chapter("ch-005")
  ↓
  Agent 在 maybeAutoCompact() 中发现 new_chapter_id = "ch-005"
  ↓
  更新 current_chapter_id_ = "ch-005"
  ↓
第 N+1 轮：
  buildSystemPrompt 才输出 ch-005 的上下文

**第 N 轮请求中 buildSystemPrompt 输出的仍然是旧章节的上下文，滞后一轮。** LLM 在上下文"错误"的情况下已经完成了一轮对话。

**2. LLM 临时读旧章节会意外切换上下文**

当前在写 ch-005，system prompt 是 ch-005 的上下文
LLM 为了参考前文调了 read_chapter("ch-001")
  ↓
  maybeAutoCompact 检测到 new_chapter_id = "ch-001"
  ↓
  current_chapter_id_ = "ch-001"
  ↓
第 N+1 轮：system prompt 变成了 ch-001 的上下文
LLM 继续写 ch-005，但 buildSystemPrompt 给的是第一章的信息

LLM 只是想参考一下，结果当前上下文被切换了。

**3. 单值存储，无法同时追踪多个章节**

`last_chapter_id_` 和 `current_chapter_id_` 都是单个字符串。如果 LLM 在一个响应中先后调用了 `read_chapter("ch-001")` 和 `read_chapter("ch-002")`（对比两章），只有最后一个被记住（`Agent.cpp:228` 直接赋值覆盖前一个）。

**4. 初始为空，导致首次写作没有章节级上下文**

启动后第一轮对话 `current_chapter_id_` 为空，`buildSystemPrompt` 只输出三行（标题+logline+theme）。LLM 的第一次请求没有任何角色/设定/规则信息，直到它调用了 `create_chapter` 后才有。而写第一个章节恰恰是最需要上下文指引的时候。

**5. 无用户控制路径**

`Agent::setCurrentChapter()` 虽然是 public 方法，但没有任何外部调用方。既没有注册为工具（LLM 不能调），也没有对应的 REPL 命令（用户不能调）。用户无法告诉系统"我现在要写第三章"。

### 建议

1. 将 `chapter_id` 追踪从"事后推断"改为"用户/系统主动声明"。增加 `/chapter` 命令或 `set_chapter` 工具，让用户或 LLM 可以主动设置当前章节
2. 考虑多章节同时活跃的场景，用 `set<string>` 或优先级列表替代单值存储
3. 首次进入项目时，如果没有指定章节，至少取第一章或最近写过的一章作为默认值，避免初始状态为空

### 执行记录

> 2026-07-11：`current_chapter_id_` / `last_chapter_id_` / `setCurrentChapter` / `maybeAutoCompact` 已全部删除。`buildSystemPrompt` 不再接收 chapter_id 参数。不再有任何"当前章节"的概念——LLM 通过 `get_latest_chapter` / `get_chapter_context` 工具按需查询。问题 1-5 全部解决。

---

## 重构方案：用 get_chapter_context 工具替代 buildSystemPrompt 的自动注入（✅ 已实施）

### 动机

前文 5 个问题的根源都可以追溯到同一个设计决策：**代码替 LLM 决定了"哪些信息必须进上下文"，并且每轮强制注入。** 一个更合理的方案是将这个决策权还给 LLM。

### 方案

当前：

每轮 LLM 请求前：
  buildSystemPrompt(project, chapter_id)
  → PromptContextBuilder::buildForChapter()
  → 输出 200-400 行 Markdown → 注入 system prompt
  → 每轮重复，不可跳过，不可选择

  chapter_id 来源：
  → Agent 事后从 tool_calls 推断（滞后一轮）
  → 单值，临时读旧章会意外切换

改为：

system prompt 精简为：
  # 项目: xxx
  Logline: xxx
  主题: xxx
  写作目标: 请先调 get_chapter_context 获取当前章节的完整上下文

LLM 按需调用：
  get_chapter_context(chapter_id="ch-003")
    → 调 PromptContextBuilder::buildForChapter()
    → 返回角色/设定/规则/大纲的完整 Markdown
    → 结果在对话消息中，LLM 基于它继续写作

### 优势

| 维度 | 当前（自动注入） | 改为工具 |
|------|----------------|---------|
| Token 开销 | 每轮 2-5 KB | 仅 LLM 主动搜索时 |
| chapter_id 滞后 | 滞后一轮 | 直接传参，即时获取 |
| 多章节同时活跃 | 不能（单值） | 可以，LLM 同时查多个章节 |
| 意外切换 | 读旧章会切上下文 | 不会，每次显式指定 |
| 信息相关性 | 系统决定"你需要这些" | LLM 自己判断需要什么 |

### 可一并清理的代码

1. `buildSystemPrompt` 的模式 2（章节级上下文）→ 移入工具，仅在调用时执行
2. `Agent::maybeAutoCompact()` → 不再需要做章节切换检测，只保留基于 `shouldAutoCompact()` 的自动压缩即可
3. `ContextManager::current_chapter_id_` → 不再需要，工具直接接收 LLM 传参
4. `buildProjectRef` 中的 `buildForChapter` 调用 → 与工具共享同一底层函数

### 注意事项

1. **增加了一轮 tool call 延迟。** 但 LLM 不需要每轮都查——如果上下文连续，查一次可以用好几轮。权衡后 token 节省远超延迟成本。
2. **system prompt 需要提示 LLM 先查再写。** 在 system prompt 或工具描述中说明"每次写作前，建议先调 get_chapter_context 获取当前章节的上下文"，让 LLM 养成习惯。
3. **保留少量调性信息。** 即使按工具方案，标题 + logline + theme + 写作目标这几行"调性信息"每轮都需要，适合留在 system prompt 里。
4. **`get_chapter_context` 和 `search_memory` 的关系。** `get_chapter_context` 获取结构化上下文（角色/设定/规则/大纲），`search_memory` 获取语义相似片段。前者精确、确定，后者模糊、灵活。两者互补，不存在重叠。

### 执行记录

> 2026-07-11：方案已实施。
> - `buildSystemPrompt` 精简为标题 + Logline + theme + `renderToolUseInstructions()`（注意事项第 3 条已遵守）
> - `maybeAutoCompact` 已删除
> - `current_chapter_id_` 已移除
> - `buildProjectRef` 已删除
> - `get_chapter_context` 工具已存在
> - `get_latest_chapter` 工具已新增（LLM 无需自己追踪章节 ID）
> - `renderToolUseInstructions` 已更新，加入 `get_latest_chapter` 和更新的写作流程建议

---

## 问题：压缩摘要注入到 system prompt 而非对话中（❌ 未修复）

### 问题

`ContextManager::assemble()` 第 411-416 行将 compactor 生成的摘要追加到 `result.system_prompt` 末尾。这在语义上是不恰当的。

### 分析

**1. 混淆了"指令"和"记忆"的层级**

- System prompt → LLM 视为最高优先级指令（"规则"）
- 压缩摘要 → 本质是对话历史的总结（"事实"）

两者混在一起后，LLM 在注意力机制中无法区分"写作风格指南（规则）"和"上一章写了什么（事实）"。如果摘要内容与指令产生张力，LLM 不知道优先级。

**2. 破坏 system prompt 缓存**

主流 API（OpenAI、DeepSeek 等）对 system prompt 内容不变时的缓存复用。每轮都在 system prompt 末尾追加内容不同的摘要，缓存全部失效，增加了延迟和成本。

**3. 优先级错位**

对话历史截断时保留了最新消息、压缩了旧消息。压缩摘要作为"被删除的旧消息的替代品"，应该放在保留的消息队列最前面（保持时间线顺序），而不是提升到 system prompt 里。

### 建议

将压缩摘要作为一条 **assistant 角色**的消息插入对话列表的头部，而非追加到 system prompt：

```
原始对话（20 条）→ 压缩后：

  ┌─ system prompt ────────────────────┐
  │  （不变，可缓存）                   │
  └──────────────────────────────────┘

  ┌─ 消息列表 ─────────────────────────┐
  │ [assistant] 以下是被压缩的对话摘要：│
  │             情节事实：...           │
  │             风格参考：...           │
  │                                    │
  │ [user] 写第二章                     │
  │ [assistant] ...                     │
  │ ...                                │
  └────────────────────────────────────┘
```

这样：
- system prompt 保持稳定，可复用 API 缓存
- 摘要是"历史事实"而非"规则指令"，层级正确
- 多次压缩时旧的摘要可以被再次压缩，不会在 system prompt 里永久累积
- 后续的强迫压缩（问题十）产生的摘要同样按此处理

> 执行记录：2026-07-11 — 未实施。当前仍将压缩摘要追加到 system prompt 末尾。

---

## 问题：assemble() 步骤 4 的告警依赖的是过时数据（❌ 未修复）

### 问题

`ContextManager::assemble()` 步骤 4（第 425-445 行）调用 `checkThresholds()` 读取 `TokenTracker` 中的 `current_context_size_`——即**上一轮请求的实际 token 数**，而非本轮组装后的实时大小。

### 分析

**1. 读的是旧数据**

```cpp
// TokenTracker 的 current_context_size_ 在上轮 assemble 步骤 7 被更新，
// 然后被 TokenTracker::record()（Agent 处理完这轮后调用）覆盖为实际 input。
// 步骤 4 读到的永远是上一轮的 size，不是本轮刚拼好的值。
```

而本轮真正的大小 `result.total_tokens` 要到步骤 6 才算出来。

**2. 功能与 shouldAutoCompact() 重叠**

`shouldAutoCompact()` 同样基于 `usagePercent()`（同一套数据）。70% 自动压缩、60% 出告警——两者判断的是同一件事，告警说"建议压缩"，自动压缩在 70% 已经做了。

**3. 即使用了实时数据，步骤 5 的截断也不受影响**

告警在步骤 4，截断在步骤 5，截断用 `msg_budget`（步骤 3 已算出）做决策。即使步骤 4 被告知"要超了"，步骤 5 也不会因此改变行为。

### 建议

删除步骤 4 的 `checkThresholds()` 告警，合并到步骤 6 之后：

```
步骤 6: total_tokens = sys_tokens + msg_tokens

替代步骤 4 和步骤 6.5：
  本轮 total_tokens / model_limit ≥ 85% → "上下文已占用 85%"
  本轮 total_tokens / model_limit ≥ 70% → "上下文已占用 70%"
  本轮 total_tokens > model_limit       → "超出模型窗口！"
```

`shouldAutoCompact()` 保持不动（process 步骤 4，用于请求前触发压缩），assemble 内部的告警改用实时数据，两件事彻底分离。

> 执行记录：2026-07-11 — 未实施。步骤 4 的 `checkThresholds()` 告警仍使用 `TokenTracker` 的过时数据。

---

## 问题：truncateMessages 是几乎不会实际执行的安全网（❌ 未修复）

### 问题

`assemble()` 步骤 5 每轮调用 `truncateMessages()`，但消息超出 `msg_budget` 的情况在实际运行中几乎不会发生。

### 分析

**1. 触发阈值差距过大**

```
shouldAutoCompact() 触发线  → 70%（主动压缩）
truncateMessages 触发截断   → 100%（msg_budget 装不下）

中间有 30% 的窗口空间 = 约 38K tokens（128K 窗口下）
```

消息从 70% 增长到 100% 需要 38K 的对话量，这期间 `shouldAutoCompact()` 早就该再次触发了。除非压缩失败、阈值设置异常，否则截断不会被执行。

**2. 代码内已有提前返回**

```cpp
if (llm::TokenCounter::countMessages(messages) <= budget) return messages;
```

绝大多数情况下消息总量小于等于预算，直接原样返回，`truncated_count = 0`，连告警都不会产生。

**3. 作为安全网是合理的**

虽然在正常情况下不触发，但它存在的价值是：万一某次压缩失败了、或者用户手动在短时间内写了大量内容、又或者模型窗口本身较小（如 16K 窗口 70% 后只有 4.8K 空间），截断至少保证请求不会超限被 API 拒绝。

### 建议

将 `truncateMessages` 替换为"强迫压缩"机制（详见问题十），截断仅作为 API 异常时的最后回退。

> 执行记录：2026-07-11 — 未实施。`truncateMessages` 仍作为唯一的安全网，强迫压缩机制未实现。

---

## 问题十：用强迫压缩替代截断作为安全网（❌ 未修复）

### 问题

当前 `truncateMessages` 在 `msg_budget` 不够时直接丢弃旧消息。对写小说来说，丢弃的可能是伏笔、角色细节——这些一次性损失远大于一次 LLM API 调用的成本。

### 方案

在 `assemble()` 步骤 5 中用强迫压缩**完全替代**截断。`truncateMessages` 仅作为压缩失败时的回退：

```
assemble() 步骤 5：

  msg_budget 不够 →
    ① 调 compact() 压缩旧消息
    ② compact() 成功后删除旧消息
    ③ 压缩摘要按问题七的方式插入消息列表头部（assistant 角色）
    ④ 重新计算 msg_budget（大幅释放）
    ⑤ 重新拼 result.messages（此时预算充足，全保留）

    compact() 失败（API 异常等）：
      → 回退到 truncateMessages 截断，保证请求能发出
```

注意步骤 ③：强迫压缩产生的摘要**不放进 system prompt**，而是作为 assistant 消息放在对话历史头部。这样 system prompt 保持稳定，且后续再次压缩时旧的摘要也会被纳入压缩范围，不会永久累积。

### 可行性

压缩请求本身的可行性不是问题：

```
假设当前用量 95%：
  system prompt（固定）+ 消息列表 = 95%

compact() 发起的 LLM 请求：
  compact_prompt + 待压缩消息
  = 约 50-60% 窗口（因为待压缩消息本身就在这 95% 里）

  → 这是 {system: compact_prompt, messages: 待压缩旧消息}
  → 不需要额外窗口空间给后续消息，因为是纯压缩操作

压缩后：
  95% → 约 15-20%（system prompt + 摘要 + 保留的 20 条消息）
```

### 优势

| 维度 | 当前（截断） | 强迫压缩 |
|------|-------------|---------|
| 信息保留 | 丢弃旧消息，永久丢失 | 摘要形式保留情节事实和风格参考 |
| 安全性 | 无信息损失风险 | 压缩失败时回退到截断 |
| 用户感知 | 静默丢消息 | 可能感知到一次延迟（但 95% 很少触发） |

### 注意事项

1. `compact()` 本身不能递归触发——压缩中的压缩可能会导致无限循环。需要在 `compact()` 入口处加一个重入守卫（如 `static std::atomic<bool> compacting`）。
2. 正常情况下 70% 的 `shouldAutoCompact()` 已经拦截了，95% 的强迫压缩应该很少触发，额外的 API 成本可忽略。
3. `compact()` 的 focus 参数可以指定为"强迫压缩：窗口即将耗尽"，让 LLM 知道这是紧急压缩。

> 执行记录：2026-07-11 — 未实施。

### 特别警告：反复压缩的信息衰减

将摘要放入消息列表（问题七方案）后，下一次压缩时旧的摘要会成为"待压缩消息"的一部分。这意味着摘要会被再次压缩——可能产生"摘要的摘要"。

问题：

- **信息逐层损失。** 每次压缩都是 LLM 的重新总结，细节会逐层丢失。"张三在第三章找到了神秘钥匙"可能最终变成"张三有进展"。
- **LLM 不擅长压缩元信息。** system prompt 要求"双层摘要：情节事实 + 风格参考"。如果待压缩内容里包含"以下是被压缩的对话摘要"的 assistant 消息，LLM 需要理解"这是一段摘要"而不是"这是对话历史"，这对 LLM 的指令遵循能力有额外要求。

缓解措施：

- 将 `kCompactKeepExchanges` 设置得偏大（如 15-20 对消息而非当前的 10 对），让摘要尽量晚被再次压缩
- 压缩 prompt 中明确提示"注意：对话中可能包含之前被压缩的摘要，请在生成新摘要时以原始摘要的情节事实为准，避免过度概括"
- 在 **强迫压缩** 流程中（问题十），触发时应优先压缩最新消息而非旧摘要——即只压缩保留边界之外的新增消息，旧的摘要部分保留不动

---

## 问题十一：assemble() 步骤 7 的状态缓存形成反馈循环

### 问题

`assemble()` 步骤 7 将 `result.total_tokens` 写入 `TokenTracker::current_context_size_`，而下一轮 `shouldAutoCompact()` 和步骤 4 的 `checkThresholds()` 都依赖这个值。由此形成了一个反馈循环：

```
第 N 轮:
  total_tokens = 12000
  setCurrentContextSize(12000)
  → record(input) 覆盖为实际 input（如 11000）

第 N+1 轮:
  shouldAutoCompact() = 11000/16000 = 69%（≈70%，可能触发）
  checkThresholds()   = 11000/16000 = 69%（告警）

第 N+1 轮:
  total_tokens = 5000（压缩后大幅缩小）
  setCurrentContextSize(5000)
  → record(input) 覆盖

第 N+2 轮:
  shouldAutoCompact() = 5000/16000 = 31%（不触发）
  checkThresholds()   = 31%（无告警）
```

### 分析

这个反馈循环是本轮正常、必须的设计——`usagePercent()` 需要基于最近一次的实际大小来做下一轮的压缩决策，这没有问题。

**但问题出在步骤 4 和步骤 6.5 用同样的数据做了两次告警：**
- `setCurrentContextSize` 本身是正确的（服务于下一轮的 `shouldAutoCompact`）
- 步骤 4 用同一个数字做告警是多余的——`shouldAutoCompact` 已经基于它做了决策
- 步骤 6.5 用实时的 `total_tokens` 做检查是对的，但只告警不阻断

> **补充观察：`setCurrentContextSize` 写入的值其实被 `record()` 覆盖了才被读取。**
> 
> 时序实际上是：
> ```
> assemble() 步骤 7: setCurrentContextSize(12000)
> → 请求发往 LLM
> → Agent 处理后调 recordUsage(input=11000, output=...)
> → TokenTracker::record() 覆盖 current_context_size_ = 11000
> → 下一轮 shouldAutoCompact() 读到的其实是 11000，不是 12000
> ```
> 
> 所以步骤 7 写入 `total_tokens` 的这行代码在正常流程中是无用的——`record()` 会在下一轮 `shouldAutoCompact` 读取之前覆盖它。只有当 `record()` 没有被调用时（请求失败、中途异常），步骤 7 的值才会保留到下一轮，作为 fallback。

### 建议

维持 `setCurrentContextSize` 不变（它是 `shouldAutoCompact` 的必要输入）。步骤 4 的告警移除，合并到步骤 6 之后用实时数据。步骤 6.5 从"告警但不阻断"升级为"阻断请求并通知用户"。

> 执行记录：2026-07-11 — 未实施。步骤 4 告警、步骤 6.5 检查逻辑均未变更。

