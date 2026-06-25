# NovelAgent 上下文管理系统 — 机制与实现

> 2026-06-25 | v0.6.1 | 基于 `src/agent/ContextManager.cpp` 实际代码逐行验证

---

## 1. 核心问题与设计目标

写小说的上下文管理不同于代码助手。三个根本矛盾驱动了整个设计：

| 矛盾 | 表现 | 解决思路 |
|---|---|---|
| 对话不能丢 vs 窗口有限 | 创作文字是产品，不能像代码工具那样丢弃工具输出 | 三层记忆 + preserved 标记 + 同步 compact |
| 角色设定持久 vs 对话变化 | 每轮对话增长，但角色设定必须跨章节一致 | L1 system prompt 从 Project JSON 重新构建，不受截断影响 |
| 历史需召回 vs 截断会丢失 | 伏笔和前文引用需要跨越数百轮对话 | 向量检索 + LLM 双层摘要（事实+风格） |

---

## 2. 三层记忆模型

```
┌──────────────────────────────────────────────────────────┐
│ L1: 长期记忆 — System Prompt                             │
│ 来源: Project JSON 文件（磁盘），每次请求重新构建         │
│ 内容: 项目元数据 + 大纲 + 角色(≤8) + 设定(≤8) +          │
│       世界规则(≤6) + 情节线(≤6) + 当前章节 + 相邻章节    │
│ 机制: PromptContextBuilder::buildForChapter()             │
│ 持久: 存在 Project JSON，不受截断/重启影响               │
│ 特点: 角色按 6 级相关性排序；按章节 order 过滤发展记录   │
├──────────────────────────────────────────────────────────┤
│ L2: 中期记忆 — Compacted Summary                         │
│ 来源: /compact 命令或自动触发（LLM 双层摘要）             │
│ 内容: 情节事实（角色决策/情节转折/伏笔/待办）            │
│       + 风格参考（2-3 句原文摘录）                       │
│ 机制: 注入 system prompt [会话历史摘要 — 当前风格参照]   │
│ 持久: session_meta.json → compacted_summary 字段         │
│ 特点: ≤500 tokens 替代数千 tokens；压缩时注入项目上下文  │
├──────────────────────────────────────────────────────────┤
│ L3: 短期记忆 — Conversation Messages                     │
│ 来源: 用户输入 + LLM 回复 + 工具调用结果                 │
│ 内容: 原始消息（user/assistant/tool）                   │
│ 机制: preserved 优先扣预算 → 最新消息反向贪心             │
│ 持久: conversation.json（完整消息列表）                  │
│ 特点: P0 保护：Pin 溢出时强制保留当前输入                │
└──────────────────────────────────────────────────────────┘
```

### 2.1 L1 实现：Project → System Prompt

**注入链**：
```
NovelAgentApp::setupAgent()
  └── cm_.setProject(project_.get())          ← 启动时注入

Agent::maybeAutoCompact()
  └── context_manager_->setCurrentChapter(id) ← 章节切换时更新

ContextManager::assemble()
  └── buildSystemPrompt(*project_, current_chapter_id_)
        └── PromptContextBuilder::buildForChapter(project, options)
              └── selectCharacters() → 6 级排序 → 截断到 max_characters(8)
```

**角色选择 6 级优先级**（`PromptContextBuilder::selectCharacters()`）：

```
优先级 1: focus_characters / pov_characters        ← 章节明确指定的焦点/POV
优先级 2: scene.pov_character_id / participants    ← 场景直接参与者
优先级 3: plot_threads.related_characters          ← 情节线关联角色
优先级 4: 本章有 development 记录的角色             ← v0.6 新增：发展记录优先于出场记录
优先级 5: 本章有 chapter_appearances 的角色         ← 出场记录
优先级 6: 截断到 max_characters(8)                 ← 数量保护
```

每级内部使用 `appendUnique()` 去重，已包含的角色不会重复添加。`GenerationControl` 对每个角色的字段做白名单/黑名单过滤，`character_development` 按章节 order 过滤（写第 5 章时只展示 1-5 章的发展记录）。

### 2.2 L2 实现：Compaction 压缩

**三种触发路径**：

```
路径 A: 百分比阈值
  Agent::processUserMessage() 入口
  → shouldAutoCompact()
  → auto_compact_ && usage_percent >= 70
  → compactConversation()

路径 B: 章节切换
  Agent::processUserMessage() 末尾
  → maybeAutoCompact()
  → 检测 create_chapter 工具调用 或 read_chapter 切换章节
  → compactConversation("切换到新章节...")

路径 C: 同步 compact（v0.6 替代 pending_compact_）
  SerialProcessor::process() 中
  → buildEffectivePrompt() 返回后
  → if (lastTruncatedCount() >= 5)
  → context_manager_->compact(conversation, client_, ...)
  → 重新 buildEffectivePrompt()
  → 再用完整上下文发送 LLM 请求
```

**压缩执行细节**（`ContextManager::compact()`）：

```
输入: conversation + ILLMClient& + 可选 focus
过程:
  1. 保留边界: 总消息数 > 20 时，保留最后 20 条不动
     待压缩: messages[0 .. total-21]
  2. 估算压缩前后 token 数
  3. 拼接待压缩消息为 "[用户] ...\n[助手] ...\n[工具] ..."
  4. 构建压缩 prompt:
     System = kCompactSystemPrompt（双层摘要指令）
            + 当前项目设定参考（前 500 字，v0.6 新增，用于代词归属）
            + 可选 focus 指令
     User   = <拼接的旧消息文本>
  5. 非流式调用 LLM: llm_client.chatNonStreaming(compact_msgs, {}, compact_prompt)
  6. 存储: compacted_summary_ = response.content
          compaction_marker_ = compact_count
  7. 返回 CompactResult{messages_compacted, tokens_before, tokens_after}
```

**压缩 prompt 原文**（`kCompactSystemPrompt`，v0.6 软约束版本）：

```
你是一个小说创作助手的上下文压缩器。用中文对以下对话历史进行双层摘要：

1. 情节事实：角色决策与性格变化、情节转折与关键事件、
   世界观设定变更、未解决的伏笔与冲突、待完成任务与下一步计划

2. 风格参考：摘录 2-3 句最能代表当前写作风格的原句——
   保留其修辞手法、句式节奏、情绪氛围和对话语气

总长度控制在 500 字以内，事实与风格的比例由你判断。
```

**为什么双层**：仅提取情节事实会导致"风格断层"——后续写作变成说明书风格。加入原文摘录后，LLM 在 system prompt 中就能看到"应该怎么写"的参照物，不需要回头找已被截断的原文。

**为什么注入项目上下文**（v0.6）：旧对话充满人称代词（"他"、"那个黑衣人"）。压缩 LLM 仅凭纯文本无法正确归属。注入前 500 字角色/设定摘要后，LLM 能将"他决定复仇"正确记录为"张三决定复仇"。

### 2.3 L3 实现：消息截断

**算法**（`ContextManager::truncateMessages()`）：

```
阶段 1: 分离
  preserved_msgs = [msg for msg in messages if msg.preserved]
  normal_msgs    = [msg for msg in messages if !msg.preserved]

阶段 2: 预算计算
  preserved_tokens = countMessages(preserved_msgs)
  remaining_budget = budget - preserved_tokens

阶段 3: P0 保护（v0.6）
  if (remaining_budget < 0) {
      日志 error "preserved 已超预算"
      强制追加 normal_msgs.back()（当前用户输入）
      return preserved_msgs + 最后一条普通消息
  }

阶段 4: 贪心截断
  result = []
  used = 0
  for msg in reverse(normal_msgs):
      cost = countSingleMessage(msg)
      if (used + cost > remaining_budget) break
      used += cost
      result.push_back(msg)

阶段 5: 组装
  reverse(result)
  result = preserved_msgs + result

兜底:
  if (result.empty()) result.push_back(messages.back())
```

**复杂度**：O(n)，单次遍历。`countSingleMessage()` 对每条消息只计算一次。

**同步 compact**（`SerialProcessor::process()` 步骤 4.5，v0.6 新增）：

```
buildEffectivePrompt() 返回后 → truncated ≥5？
  ├── 是 → compact(conversation, client_, focus)
  │        → 如果 messages_compacted > 0
  │        → 重新 buildEffectivePrompt()（摘要已注入，截断大幅减少）
  │        → 再进入 ToolCallLoop
  └── 否 → 直接进入 ToolCallLoop
```

与 v0.5 的 `pending_compact_` 异步标记的区别：v0.5 是在"本轮截断→标记→下轮才压缩"，本轮已用残缺上下文的劣质回复写进了对话历史。v0.6 改为同步——截断当场压缩重建，不给劣质回复留机会。

**最终预检**（`assemble()` 末尾）：

```
final_total = sys_tokens + msg_tokens
if (final_total > model_context_limit)
    → warnings: "总 token 超出模型窗口，请求可能被 API 拒绝"
    → spdlog::error 日志
    → 不强制降级（设计取舍，见 §13）
```

---

## 3. 完整数据流

```
用户输入 "写第三章第二节..."

1. Agent::processUserMessage()
   ├── validateInput()           — 长度 ≤64K + 6 种注入模式拦截
   ├── StateMachine 守卫          — Thinking 状态拒绝新输入
   ├── shouldAutoCompact()        — 百分比阈值检查（auto_compact_ && ≥70%）
   │   └── 触发 → compactConversation()
   └── Idle → Thinking

2. SerialProcessor::process()
   ├── conversation.addUser(input)
   ├── buildEffectivePrompt()
   │   └── ContextManager::assemble(conversation, max_context_tokens)
   │       ├── [1] buildSystemPrompt(project_, current_chapter_id_)
   │       │      → PromptContextBuilder::buildForChapter()
   │       │      → 项目元数据 + 大纲 + 角色(≤8) + 设定(≤8) + 世界规则(≤6) + 情节线(≤6)
   │       ├── [1.5] 向量检索
   │       │      ├── vector_store_dirty_? → skip（/rewind 后）
   │       │      ├── isVectorStoreStale()? → warning（Project 更新后）
   │       │      ├── last user 消息 → generateEmbedding → search(top3)
   │       │      └── 注入 [相关历史片段 — 仅作事实参考] + 章节标签
   │       ├── [2] 注入 compacted_summary_（如果有）
   │       │      → [会话历史摘要 — 当前风格参照]
   │       ├── [3] truncateMessages(messages, msg_budget)
   │       │      → preserved 扣预算 → 反向贪心 → P0 保护 → 兜底
   │       ├── [4] 生成 warnings（截断/用量/过期）
   │       └── [5] 缓存 last_warnings_ + last_truncated_count_
   │
   ├── ★ 同步 compact 检查
   │   └── lastTruncatedCount() ≥ 5 → compact() → 重建 prompt
   │
   ├── ToolCallLoop::run()
   │   ├── LLM chat（首轮流式，后续非流式）
   │   ├── 多轮 tool_call → 执行工具 → 追加结果 → 再次 LLM
   │   └── 返回 ToolCallLoopResult{input_tokens, output_tokens, ...}
   │
   ├── recordUsage(input_tokens, output_tokens) → token_state_ 累计
   └── 追加 assistant 消息 → conversation

3. Agent
   ├── maybeAutoCompact()
   │   ├── create_chapter 检测 → compact + 更新 chapter_id
   │   └── read_chapter 切换 → compact + 更新 chapter_id
   └── 返回 LLMResponse

4. ReplHandler
   ├── 打印 warnings + compaction 提醒（≥50%）
   └── saveSessionState() → conversation.json + session_meta.json
```

---

## 4. Token 预算

```
max_context_tokens（默认 131072，用户可配）
    │
    ├─ sys_tokens = countTokens(system_prompt)
    │   = PromptContextBuilder(~2-5K)
    │   + 向量检索结果(~200-500，dirty 时 = 0)
    │   + 压缩摘要(~0-500)
    │
    └─ msg_budget = max(0, max_context_tokens - sys_tokens)
        │
        ├─ preserved_tokens = countMessages(preserved_msgs)
        ├─ remaining = msg_budget - preserved_tokens
        │   ├─ < 0 → 仅保留 preserved + 当前输入（P0 保护）
        │   └─ ≥ 0 → 最新消息反向贪心保留
        └─ 最终预检: total > model_context_limit → warning
```

**会话级累计**：`recordUsage(input, output)` 每轮累加，用于 `usagePercent()`、`checkThresholds()`、持久化。

**注意**：`max_context_tokens` 不是模型窗口大小，是应用层成本控制上限。模型真实窗口可能更大（如 200K）。

---

## 5. 消息保留（Pin）机制

- `Message::preserved` — 不参与 JSON 序列化（API 不需要），仅内部使用
- 截断时优先扣除预算，但**计入** budget（v0.5）
- P0 保护（v0.6）：preserved 超预算时，强制保留当前用户输入
- 持久化：`SessionMeta::preserved_indices` → 重启时重新标记
- `compact()` **不删除**任何消息（preserved 的消息完整保留）

CLI：`/pin last|<N>`、`/unpin <N>`、`/pins`

---

## 6. 向量检索机制

### 6.1 两层前置检查（v0.6.1）

```
检索前:

检查 A: vector_store_dirty_?（/rewind 后置位）
  → true → 跳过检索 + warning "向量索引在 /rewind 后已标记为过期，建议 /index 重建"
  → goto skip_retrieval

检查 B: isVectorStoreStale()?（project.json mtime > vectors.json mtime）
  → true → warning "Project 已更新，向量索引可能过期。建议 /index 重建"
  → 仍执行检索（章节文本嵌入仍然有效，元数据变更不否定文本相关性）

通过后:
  → last user 消息 → embedding API → search(top3) → 注入带标签片段
```

### 6.2 注入格式

```
[相关历史片段 — 仅作事实参考，风格以当前上下文为准]

片段1 (ch-003, 相似度85%): 主角在山洞中发现古剑...
片段2 (ch-001, 相似度72%): 师父交代身世之谜...
片段3 (ch-005, 相似度68%): 反派在城中散布谣言...
```

### 6.3 风格冲突解决

检索片段可能来自早期章节（如 ch-001 轻松风），L2 compact 摘要来自当前章节（如 ch-005 沉重风）。通过标签明确区分：
- `[仅作事实参考，风格以当前上下文为准]` — 检索片段
- `[当前风格参照]` — L2 compact 摘要

LLM 被明确告知：事实可参考检索片段，写作风格跟随 L2。

### 6.4 容错

- 检索失败（网络超时、API 报错）→ `spdlog::warn`，不阻断 `assemble()`
- freshness check 文件不存在 → 静默跳过
- `vector_store_dirty_` → 跳过检索，不报错

---

## 7. 会话生命周期

### 7.1 状态机

```
Idle ──(用户输入)──→ Thinking ──(LLM 响应)──→ Idle
  ↑                     │                        │
  └──(Error恢复)────────┴──(异常)──→ Error ──────┘
```

### 7.2 操作效果

| 操作 | 会话影响 | 上下文影响 | 持久化影响 |
|---|---|---|---|
| `processUserMessage()` | 追加对话 + LLM 多轮 | 自动 compact 检查 + 同步 compact | 自动保存 |
| `/clear` | 清空对话 | 重置统计 + 清除摘要 + warnings 清除 | 不保存 |
| `/rewind N` | truncateTo(N+1) | 清除失效摘要 + 标记向量脏 | 下次保存时写入 |
| `/edit N text` | 修改消息内容 | 不影响（下次 assemble 生效） | 下次保存时写入 |
| `/compact [focus]` | 不变 | 生成双层摘要 + 清除向量脏标记 | 自动保存 |
| 退出 | — | — | saveSessionState() |
| 启动 | 恢复对话 | 恢复元数据 + mtime 检查 | loadSessionState() |

### 7.3 持久化文件

```
.novelagent/
├── conversation.json         ← 完整消息列表（role/content/tool_calls/tool_call_id）
├── session_meta.json         ← 会话元数据
│   {
│     "compacted_summary": "...",
│     "compaction_marker": 42,
│     "token_state": { total_input: 125000, total_output: 45000, ... },
│     "last_chapter_id": "ch-003",
│     "preserved_indices": [5, 12, 28],
│     "project_mtime": 1719345600
│   }
├── vectors.json              ← 章节文本嵌入向量
└── traces/                   ← 执行轨迹
```

`project_mtime` 通过 `std::filesystem::last_write_time` 自动获取，加载时对比，不一致则清空旧摘要。

---

## 8. 状态一致性保障

系统在 8 个跨组件场景中维护状态一致性：

| # | 场景 | 问题 | 保障机制 | 版本 |
|---|---|---|---|---|
| 1 | `/rewind` 回滚 | 摘要含"未来"信息 | 回滚位置 ≤ compaction_marker → clearCompactedSummary | v0.5 |
| 2 | Pin 溢出 | preserved 超预算 → 当前输入被丢弃 | remaining_budget < 0 → 强制保留最后一条普通消息 | v0.6 |
| 3 | 章节切换 | 旧对话 + 新设定 → LLM 混淆 | maybeAutoCompact 检测 create/read_chapter 切换 → 自动 compact | v0.5 |
| 4 | Project 修改 | 旧摘要(旧名) + 新设定(新名) → 矛盾 | session_meta 记录 project_mtime，std::filesystem 自动获取/对比 → 清空 | v0.5 |
| 5 | 突发截断 | 本轮用残缺上下文 → 劣质回复写入历史 | SerialProcessor 同步 compact：截断≥5 → 立即重建 prompt | v0.6 |
| 6 | 向量过期(元数据) | Project 元数据更新后向量索引未重建 | assemble() freshness check：mtime 对比 → warning | v0.6 |
| 7 | 压缩指代不明 | 纯文本对话中代词无法归属 | compact() 注入当前角色/设定上下文（前 500 字） | v0.6 |
| 8 | 向量过期(回滚) | /rewind 后 vectors 含"未来章节"嵌入 | clearVectorStore() → vector_store_dirty_ → skip_retrieval | v0.6.1 |

---

## 9. 安全机制

**输入校验**（`Agent::validateInput()`，在 `processUserMessage()` 入口）：
- 长度 ≤ 64K 字符
- 拦截 6 种 prompt injection 模式：`<|im_start|>`、`<|im_end|>`、`忽略以上指令`、`ignore all previous`、`[system]`、`[SYS]`、`<|endoftext|>`
- 校验失败 → 记录 ExecutionTracer + 返回空 LLMResponse

**工具安全**：`ShellTools::isDangerousCommand()` 黑名单 ~40 关键词，输出上限 100KB。

---

## 10. Prompt Cache

`ProviderConfig::supports_cache_control == true`（仅 Anthropic API）时，`LLMClient::buildRequestBody()` 对 system prompt 和最后 2 条消息标记 `cache_control: {type: "ephemeral"}`。OpenAI/DeepSeek 自动缓存 prefix。

---

## 11. 关键设计决策

| 决策 | 理由 | 版本 |
|---|---|---|
| ContextManager 持有 `Project*`（非拥有） | 避免接口膨胀；生命周期由 NovelAgentApp 保证 | v0.5 |
| `truncateMessages` 从最新反向贪心 | O(n)；写小说场景最近对话最重要 | v0.4 |
| Compaction 保留 10 对交换 | 经验值：足够保持连贯 + 大幅压缩 | v0.4.5 |
| 双层摘要（事实 + 风格原句） | 解决"风格断层" | v0.5 |
| 压缩时注入项目上下文 | 解决"代词归属"——压缩 LLM 正确识别人物 | v0.6 |
| preserved 计入 budget 但优先 | 防 API 400；溢出时强制保留当前输入 | v0.5/v0.6 |
| 同步 compact 替代异步标记 | 截断≥5 当场重建，不给劣质回复留机会 | v0.6 |
| 向量检索 freshness check | project.json mtime vs vectors.json mtime | v0.6 |
| `/rewind` 标记向量脏 | 跳过检索防"剧透污染"，用户 /index 重建 | v0.6.1 |
| 风格来源标签 | 检索标 [事实参考]，L2 标 [风格参照] | v0.6 |
| 角色按章节相关性排序 | development → appearances → 其余 | v0.6 |
| 向量检索失败不阻断 | 写作不能因检索中断 | v0.4.5 |
| `project_mtime` 绑定摘要 | std::filesystem 自动获取/对比，内部闭环 | v0.5 |
| `pinnedIndices()` 实时遍历 | 截断/回滚后自动反映当前状态，无需手动维护 | v0.4.5 |

---

## 12. 已知限制与设计取舍

以下经过了多次审查验证，确认为**有意的设计取舍**，非缺陷：

| 限制 | 原因 | 风险缓解 |
|---|---|---|
| 最终预检仅警告不强制降级 | 用户可调 `max_context_tokens`；强行降级更困惑 | 日志 error + REPL 展示警告 |
| freshness check (Project 更新) 不跳过检索 | 元数据变更 ≠ 章节文本失效；跳过检索损失更大 | warning 提示 /index 重建 |
| compact 上下文注入 500 字 | 启发式值，覆盖项目元数据 + 角色概要 | 压缩 LLM 仍可从对话文本推断 |
| compact 不跳过 preserved 消息 | compact 只生成摘要不删除消息，preserved 原样保留 | LLM 以原始消息为准 |
| `/rewind` 后需手动 /index | 个人工具场景下 /rewind 频率低；自动重建成本高 | 跳过检索 + warning 提示 |
| 工具定义全量发送 | 17 个工具 ~3-5K tokens/请求 | 后续迭代：按需加载 |
| Token 估算用启发式（字符数/4） | 精确 token 需 API 返回 | 使用 API 返回的 `prompt_tokens` 做精确累计 |

---

## 13. 版本演进

| 版本 | 日期 | 关键变更 |
|---|---|---|
| v0.3 | 2026-05 | 初始：预算分配(50/30/20) + 5 级降级 + 规则摘要 + 章节缓存 |
| v0.4 | 2026-06 | 精简：删除 BudgetAllocation/DegradationPipeline/ConversationSummarizer/ChapterSummaryCache；引入三层记忆 |
| v0.4.5 | 2026-06 | 增强：会话级 Token 追踪、Pin、Compaction、向量检索、Prompt Cache、中毒防御 |
| v0.5 | 2026-06 | 修复：Project 上下文注入、回滚清除摘要、preserved 预算预检、章节切换 compact、project_mtime |
| v0.6 | 2026-06 | P0/P1：Pin 溢出保护、向量 freshness check、压缩上下文注入、角色排序、同步 compact、风格标签 |
| **v0.6.1** | **2026-06** | **P0：/rewind 向量 invalidation（vector_store_dirty_ + skip_retrieval）** |

---

## 14. CLI 命令参考

| 命令 | 功能 | 版本 |
|---|---|---|
| `/context` | 上下文用量明细 + 活跃警告 | v0.4.5 |
| `/compact [焦点]` | 双层摘要压缩（含项目上下文） | v0.4.5 |
| `/pin last\|<N>` | 保留消息（优先保留，计入 budget） | v0.4.5 |
| `/unpin <N>` | 取消保留 | v0.4.5 |
| `/pins` | 列出保留消息 | v0.4.5 |
| `/rewind <N>` | 回滚 + 清除失效摘要 + 标记向量脏 | v0.5/v0.6.1 |
| `/edit <N> <内容>` | 编辑消息（仅 User/Assistant） | v0.5 |
| `/clear` | 重置整个会话 | v0.4 |
| `/config max_context_tokens <N>` | 上下文预算 | v0.4.5 |
| `/config auto_compact on\|off` | 自动压缩开关 | v0.6 |
| `/index` | 查看索引状态 | v0.4.5 |

---

## 15. 文件清单

```
src/agent/ContextManager.h/cpp       — 编排器（组装/压缩/检索/追踪/持久化/freshness + dirty check）
src/agent/ContextManagerTypes.h      — DTO（ContextAssembly/SessionTokenState/CompactResult/...）
src/agent/SessionPersistence.h/cpp   — 持久化（session_meta + project_mtime + SessionMeta）
src/agent/PromptContextBuilder.h/cpp — 系统提示构建（6 级角色排序 + 章节过滤）
src/agent/PromptComposer.h           — 提示拼接（personality + context → system prompt）
src/agent/Agent.h/cpp                — Agent 入口（校验/自动compact/章节检测/回滚/编辑/持久化）
src/agent/IMessageProcessor.h/cpp    — 消息处理（同步 compact + Token 追踪）
src/agent/ToolCallLoop.h/cpp         — 工具循环（input/output token 拆分）
src/agent/ExecutionTracer.h/cpp      — 执行轨迹记录
src/llm/Conversation.h               — 对话容器（pin/edit/truncateTo/pinnedIndices）
src/llm/Message.h                    — 消息/工具/响应数据结构（preserved 字段）
src/llm/LLMClient.cpp                — LLM 客户端（cache_control + buildRequestBody）
src/llm/TokenCounter.h/cpp           — Token 估算（中英文混合）
src/retrieval/VectorStore.h/cpp      — 向量存储（JSON + 暴力余弦）
src/retrieval/EmbeddingGenerator.h/cpp — 嵌入生成（OpenAI embedding API）
src/retrieval/NovelChunker.h/cpp     — 文本分块（场景/段落边界 + 15% 重叠）
src/config/AppConfig.h               — ProviderConfig（max_context_tokens/supports_cache_control）
src/cli/ReplHandler.cpp              — 交互入口（11 个上下文管理命令）
```
