# NovelAgent 审查状态总览

> 最后更新：2026-07-16（追加 §十 TokenTracker 审查）

---

## 一、代码审查（DESIGN_REVIEW_CODE.md）— 28 项，已清零

| 状态 | 数量 |
|------|------|
| ✅ 已修复 | 20 |
| 📋 延后（非 bug） | 1（Issue 12: 模型序列化分离，PCH 已覆盖编译开销） |
| ❌ 不属实 | 1（Issue 20: A17 已修复） |
| ~~误判/夸大~~ | 6（见 `REVIEW_FALSE_POSITIVES.md`） |

**结论**：无待修 bug。

---

## 二、设计合理性评审（DESIGN_REVIEW.md）— 49 项

### 已修复（35 项）

> 修复批次：① (1674246) → ② (1b19a2b) → ③ (460e5aa) → ④ (6dc457a) → ⑤ (0f68d0a) → A6 (58c0dcc) → 收尾 (A17+C6+A10)

**维度一·长篇创作一致性（18 项）**

| # | 问题 | 严重度 | 修复内容 |
|---|------|--------|----------|
| A1 | compact() 不删除已压缩消息 | 高 | compact 签名改为非 const，删除旧消息 |
| A2 | 语义检索子系统空转 | 高 | 接通 index 命令，向量库可写入 |
| A3 | 三层融合排序仅存于 PLAN.md | 高 | 改用分层标签 + chapter_id 去重，PLAN.md 已清理 |
| A4 | 摘要 500 字上限 | 高 | 改为无硬限制，追求完整摘要 |
| A5 | project.json 错路径 | 高 | 修正为 novel.json，mtime 一致性保障恢复正常 |
| A6 | 跨实体引用无完整性保护 | 高 | 软校验覆盖全部 update_* 工具 |
| A7 | 5 个核心模型对 LLM 不可写 | 高 | 补齐 Scene/Volume/PlotThread/Relationship/Development 写入工具 |
| A8 | 没有 delete/remove 工具 | 高 | 新增完整 delete 工具系列 |
| A9 | 贪心截断不识别设定类消息 | 中 | 识别关键设定消息，给予保留优先级 |
| A10 | 向量检索仅用最后一条 user 消息 | 中 | 拼接最近 3 条用户消息做查询 |
| A11 | NovelChunker 中文适配缺陷 | 中 | 中文场景标记/段落分块/UTF-8 边界修复 |
| A13 | word_count/current_word_count 无写入点 | 中 | create/update_chapter 工具补充写入 |
| A14 | CharacterDevelopment 通道断开 | 中 | 添加 CharacterDevelopment 写入工具 |
| A15 | 32KB 截断在 JSON 字符串中间切割 | 中 | 改为对象层面截断 |
| A17 | list_chapters 不按 order 排序 | 低 | 排序修复 |
| A18 | 并行编排误判 + SubAgent 能力不足 | 高 | 编排规则收紧，tool_rounds 增加 |
| A19 | 汇总策略截断 800 字 | 中 | 截断阈值提高或移除 |

**维度二·数据可靠性/崩溃恢复（5 项）**

| # | 问题 | 严重度 | 修复内容 |
|---|------|--------|----------|
| B1 | writeText 非原子 | 高 | 改为 write-to-temp-then-rename 模式 |
| B2 | 会话仅退出时保存 | 高 | 每轮对话后增量保存 conversation.json |
| B3 | SubAgent 超时 use-after-free | 高 | 改为不捕获 this，超时后安全销毁 |
| B5 | 主循环无超时 | 高 | 添加请求超时机制 |
| B8 | 异常后状态卡 Thinking | 中 | 异常路径强制 transition(Idle/Error) |

**维度三·安全与工具合理性（4 项）**

| # | 问题 | 严重度 | 修复内容 |
|---|------|--------|----------|
| C1 | Shell 工具过度授权 | 高 | 白名单模式（只读 cmdlet） |
| C2 | 危险命令黑名单易绕过 | 高 | 改为白名单 + 字符级拦截 + 黑盒测试 |
| C3 | 跨实体 ID 引用 | 中 | 见 A6，统一软校验 |
| C6 | create_chapter 不校验 ID 唯一性 | 中 | 添加 ID 唯一性检查 |

**维度四·架构抽象与过度设计（2 项）**

| # | 问题 | 严重度 | 修复内容 |
|---|------|--------|----------|
| D1 | 状态机从未触发 | 高 | 补全 state_ 转换路径，移除死代码状态 |
| D2 | config 字段名漂移 | 高 | 统一为 max_context_tokens，旧 config 自动迁移 |

### 暂缓（14 项）

| # | 类别 | 问题 | 不修理由 |
|---|------|------|----------|
| A12 | 1.嵌入刷新 | 章节更新后向量不自动失效 | `markDirty()`+Issue 5 已解决 Project 部分；向量自动刷新需增量索引，工程量大 |
| A16 | 1.规则冲突 | WorldRule 缺 contradicts_with/precedence 字段 | 当前无自动化冲突检测需求，字段可手动 JSON 添加 |
| B4 | 2.写入安全 | write_chapter 全量覆写无版本/备份 | Issue 5 的 B4 修复已加自动备份到 `.novelagent/chapters_backup/` |
| B6 | 2.向量持久化 | vectors.json 全量覆盖，崩溃可能损毁 | 向量索引可 `/index` 重建；增量持久化需 sqlite-vec |
| B7 | 2.文件锁 | 多进程同时打开同项目互相覆盖 | 使用场景为单用户 CLI，多进程冲突属边缘场景 |
| C4 | 3.参数校验 | additionalProperties 只 warn 不阻断 | LLM 传未知字段不造成数据损坏，浪费一轮调用（可接受） |
| C5 | 3.Tool Schema | update_* fields 是空 object | Issue 1 修复后 C5 已处理：fields schema 已显式列出全部可更新字段 |
| C7 | 3.跨平台 | 非 Windows 分支无超时 | 项目 Windows 优先，Unix 适配留待后续 |
| C8 | 4.Style | 6 个旋钮 free-string 无 enum | Style 字段天然自由形式（写作风格变体极多），enum 反而不灵活 |
| D3 | 4.抽象 | IMessageProcessor 抽象维度偏离领域 | 当前串行/并行二选一足够；PlanModeProcessor 等第三模式暂无需求 |
| D4 | 4.抽象 | IStorageBackend 抽象错位 | 已在 Issue 23 修复中删除该接口，改用具体 FileStorageBackend |
| D7 | 4.过度设计 | IParallelDetector 单实现 | 接口存在便于测试 Mock，当前无替换需求不构成问题 |
| D8 | 4.文档含混 | sqlite-vec 三处说法不一 | sqlite-vec 未实际引入，文档差异不影响运行 |
| D9 | 4.数值包装 | cosine 映射 [0,1] 后再百分比 | 显示值偏高但相对排序不变，对 LLM 召回行为无影响 |
| D10 | 4.文档矛盾 | overlap 10% vs 15% | 实际使用 15%，10% 的 PLAN.md 引用已删除 |

**结论**：14 项全部非 bug——要么已由其他修复覆盖（B4/C5/D4），要么是纯设计偏好/边缘场景/文档差异。

---

## 三、DEFERRED.md — 1 项暂缓（已含在设计合理性评审统计中）

Volume 与 Chapter 同级字段语义重叠——注释已标注差异，运行时无歧义。

---

## 四、已知的非问题（REVIEW_FALSE_POSITIVES.md）

AI 审查工具产生的 6 项误判/夸大（论述偏差、程度夸大、标准误读、设计≠bug、误读设计意图、代码过期）。详见该文档，作为后续审查校准参考。

---

## 五、Compactor/上下文管理重构 — 12 项，已清零

> 记录日期：2026-07-10 · 最终验证：2026-07-14 · 全部已修复

| # | 问题 | 类别 | 修复方式 |
|---|------|------|----------|
| 1 | `buildProjectRef` 1200 字节截断多余 | 代码缺陷 | 整体删除 |
| 2 | `buildProjectRef` 放在 Compactor 内部不合理 | 设计问题 | 随函数删除 |
| 3 | 章节切换自动 compact (`maybeAutoCompact`) | 设计问题 | 函数删除 |
| 4 | `current_chapter_id_` 追踪断裂（5 个子问题） | 设计问题 | 全套机制移除，全索引模式 |
| 5 | 用 `get_chapter_context` 替代 system prompt 自动注入 | 重构方案 | 已实施 |
| 6 | 去重逻辑半成品 (`covered_ids`) | 代码缺陷 | 随 `current_chapter_id_` 移除 |
| 7 | `assemble()` 自动向量检索不可控 | 设计问题 | 步骤 1 删除，`search_memory` 工具替代 |
| 8 | 压缩摘要注入 system prompt 而非对话中 | 设计问题 | 改为 user/assistant 消息对 |
| 9 | `truncateMessages` 安全网几乎不触发 | 无用代码 | 整体删除截断机制 |
| 10 | 告警依赖过时数据 (上一轮 token 数) | 代码缺陷 | 基于实时 `total_tokens` |
| 11 | 强迫压缩替代截断作为安全网 | 重构方案 | `assemble()` 内置基于实时用量的自动压缩 |
| 12 | `assemble()` 步骤 7 状态缓存反馈循环 | 代码缺陷 | `shouldAutoCompact` 改用实时传参，打破循环 |

**最终结论**：无待修项。重构后 Compactor 已合并到 ContextManager，`buildSystemPrompt` 精简为标题+工具指令，LLM 通过 `get_chapter_context`/`get_latest_chapter`/`search_memory` 按需获取上下文。

---

## 最终结论

**当前无待修 bug。** 所有剩余标记均为中/低严重度的设计偏好或边缘场景。

---

## 六、架构审查（ARCHITECTURE_REVIEW_2026-06-30.md）— 11 项

### 已修复/已关闭（8 项）

**已即时修复（首批）**

| # | 问题 | 修复方式 |
|---|------|---------|
| — | 反射消息角色伪造 | 增加角色校验 |
| — | ParallelProcessor 上下文盲区 | 补充上下文传递 |

**A 批**

| # | 问题 | 修复方式 |
|---|------|---------|
| A1 | 串行线程创建（超时机制） | 设置 ToolCallLoop timeout=300s 防止主线程永久阻塞，后改为 0s 依赖 HTTP read_timeout |
| A2 | 持久化问题 | 已验证安全的现有行为 |
| A3 | SubAgent 轨迹可见性 | 补充轨迹冒泡 |
| A4 | ReAct 预思考步骤 | ~~完善预思考逻辑~~ → 已清理（`use_thinking_step` 字段及代码块删除，`plan_mode.md` 标记 obsolete），待 Plan Mode 从零设计 |

**B 批**

| # | 问题 | 修复方式 |
|---|------|---------|
| B1 | `isParallelEnabled()` 用 `dynamic_cast` | 改为 `IMessageProcessor::isParallel()` 虚方法 |
| B3 | `TokenTracker::record()` 只记 input_tokens | 新增 `last_output_tokens_` 追踪输出 token |
| B4 | `Conversation::messages()` 每次深度复制 | 改为返回 `const std::vector<Message>&` 零拷贝 |
| B6 | `ExecutionTracer` payload 非强类型 | 改用 `TracePayload` variant 结构化类型 |

**C 批**

| # | 问题 | 关闭原因 |
|---|------|---------|
| C2 | `REGISTER_TOOL` 静态初始化器膨胀 | 自注册的 DX 收益大于静态初始化的开销，工具数达到 50+ 前无需处理 |
| C3 | `ConversationDiff::pinned_indices` 偏移风险 | 注释已修正为 `diff.added` 内部索引 |
| C5 | ShellTools 别名覆盖不全 | false negative 不影响安全性，后续工具数稳定后再考虑补全 |
| C6 | ThreadPool.h 注释是英文 | 注释已完整汉化 |

### 待处理（3 项）

见 `ARCHITECTURE_REVIEW_2026-06-30.md`：B2（无模板并行降级）、C1（硬编码超时）、C4（RewindTo 丢弃 pinned 消息）。

---

## 七、工具注册缺失（TOOL_REGISTRATION_GAP.md）— 4 项，已修复

> 发现日期：2026-07-14 · 修复日期：2026-07-15

| # | 工具名 | 类名 | 文件 | 状态 |
|---|--------|------|------|------|
| 1 | `get_chapter_context` | `GetChapterContextTool` | `ChapterContextTools.cpp` | ✅ 已补充 `REGISTER_TOOL` |
| 2 | `get_relevant_characters` | `GetRelevantCharactersTool` | `RelevantCharacterTools.cpp` | ✅ 已补充 `REGISTER_TOOL` |
| 3 | `get_relevant_settings` | `GetRelevantSettingsTool` | `RelevantSettingTools.cpp` | ✅ 已补充 `REGISTER_TOOL` |
| 4 | `get_relevant_world_rules` | `GetRelevantWorldRulesTool` | `RelevantWorldRuleTools.cpp` | ✅ 已补充 `REGISTER_TOOL` |

> 这 4 个工具已在编译中但缺少注册宏，LLM 在提示词中被指导使用它们却永远收到"工具未找到"——静默失效已修复。

---

## 八、`initial_messages` 参数审查（INITIAL_MESSAGES_REVIEW_2026-07-16）— 5+1 项，已清零

> 发现日期：2026-07-16 · 修复日期：2026-07-16（`initial_messages` 移除 + 计时删除 + tracer 删除）

| # | 问题 | 修复方式 |
|---|------|---------|
| 1 | 陈旧快照问题：`initial_messages` 与 `conversation` 不同步 | 移除参数，首轮统一使用 `conversation.messages()` |
| 2 | 首轮与后续轮次数据源不一致 | 同上，两阶段统一数据源 |
| 3 | `conversation.messages()` fallback 死代码 | 删除三元表达式 |
| 4 | `const auto&` 跨栈帧引用风险 | 移除参数 → 无跨栈引用 |
| 5 | 设计冗余：`initial_messages` 已无实际用途 | `buildEffectivePrompt` 同步移除 `out_messages` 传出参数 |
| 6 | 不必要计时：`steady_clock::now()` + `tracer_->record()` 全部无消费者 | 删除全部 8 次时钟调用 + 10 处 `tracer_->record()` 调用 |

**涉及文件**：`ToolCallLoop.h` / `ToolCallLoop.cpp` / `IMessageProcessor.{h,cpp}` / `SubAgent.cpp`

---

## 九、命名问题审查（NAMING_ISSUES_REVIEW_2026-07-16）— 1 项，已清零

> 发现日期：2026-07-16 · 修复日期：2026-07-16

| # | 问题 | 修复方式 |
|---|------|---------|
| 1 | `effective_prompt` 命名歧义：变量名为"prompt"但实际存储系统提示词，与 `effective_messages` 形成误导性对仗 | 重命名为 `effective_system_prompt`，涉及 `SerialProcessor::process()`、`ParallelProcessor::process()`、`Agent::execute()` 三处 |

**涉及文件**：`IMessageProcessor.cpp` / `Agent.cpp`

---

## 十、ToolCallLoop 超时机制问题（TIMEOUT_MECHANISM_REVIEW_2026-07-17）— 待处理

> 发现日期：2026-07-17

| # | 问题 | 状态 |
|---|------|------|
| 1 | `std::async` 超时后不杀线程，lambda `[&]` 捕获的局部引用变成悬空指针 → UB | 📋 计划删除 |
| 2 | 写小说是长任务，硬超时可能打断正常执行 | 📋 计划删除 |
| 3 | `max_rounds` + `cancelled_` 已提供足够的安全网，timeout 冗余 | 📋 计划删除 |

**解决方案**：直接删除整个 timeout 机制（`config.timeout`、`setTimeout()`、`timed_out` 字段、超时分支）。  
**详见** `TIMEOUT_MECHANISM_REVIEW_2026-07-17.md`

---

## 十一、ToolCallLoopResult token 字段归属问题（TOKEN_FIELDS_OWNERSHIP_REVIEW_2026-07-17）— ✅ 已解决

> 发现日期：2026-07-17 · 修复日期：2026-07-18

| # | 问题 | 状态 |
|---|------|------|
| 1 | `total_tokens_used` 无外部消费者，仅自身 warning 检查 | ✅ 已删除；warning 检查一并移除（Agent 通过 ContextManager 管理预算） |
| 2 | `input_tokens` / `output_tokens` 注释不准确 | ✅ 已删除；`ToolCallLoopResult` 不再包含 token 字段 |
| 3 | Agent 与 SubAgent token 记录方式不统一 | ✅ 已统一：两者都通过 `on_round_complete` hook 记录 |

**方案**：`ToolCallLoopResult` 不再承担 token 传递职责。Agent 走 hook → `ContextManager::recordUsage()`；SubAgent 也走 hook → `SubAgentResult` → `Orchestrator`。

---

## 十二、反思机制名不副实问题（REFLECTION_MECHANISM_REVIEW_2026-07-17）— ✅ 已删除

> 发现日期：2026-07-17 · 修复日期：2026-07-18

| # | 问题 | 状态 |
|---|------|------|
| 1 | 反思仅注入模板消息，不做实质分析 | ✅ 已删除整个机制 |
| 2 | 跳过 `pipeline.execute()`，不知道工具返回了什么 | ✅ 已删除 |
| 3 | 模板消息不提供具体的修正方向 | ✅ 已删除 |
| 4 | 每轮反思浪费一次 LLM 调用（Token 成本） | ✅ 已删除 |

**解决方案**：不修复，直接删除。重复调用检测后直接以 `loop_detected` 终止，不再尝试"反思→重试"循环。

**涉及文件**：`ToolCallLoop.h` / `ToolCallLoop.cpp` / `ExecutionTracer.h` / `ExecutionTracer.cpp` / `test_tool_call_loop.cpp`

---

## 十三、Token 计数字段命名不一致（TOKEN_FIELD_NAMING_REVIEW_2026-07-17）— ✅ 无需修改

> 发现日期：2026-07-17 · 评估日期：2026-07-18

| # | 问题 | 状态 |
|---|------|------|
| 1 | `LLMResponse` 用 `prompt_tokens`/`completion_tokens`，`ToolCallLoopResult` 用 `input_tokens`/`output_tokens`，两套命名混用 | ✅ 无需修改 |

**评估结论**：两套命名处于不同抽象层（API DTO vs 业务结果），`response.prompt_tokens → r.input_tokens` 是层间合法映射。统一命名收益低（仅消除几行视觉差异），成本高（需 JSON 兼容层或大面积重命名）。

---

## 十四、ToolCallLoop 超时机制问题（TIMEOUT_MECHANISM_REVIEW_2026-07-17）— ✅ 已删除

> 发现日期：2026-07-17 · 修复日期：2026-07-18

| # | 问题 | 状态 |
|---|------|------|
| 1 | `std::async` 超时后不杀线程，`[&]` 捕获的局部引用变成悬空指针 → UB | ✅ 已删除整个 timeout 机制 |
| 2 | 写小说是长任务，硬超时可能打断正常执行 | ✅ 已删除 |
| 3 | `max_rounds` + `cancelled_` 已提供足够的安全网，timeout 冗余 | ✅ 已删除 |

**方案**：直接删除。`ToolCallLoopConfig` 不再有 `timeout` 字段，`ToolCallLoopResult` 不再有 `timed_out` 字段，`std::async`/`<future>` 不再使用，循环体直接同步执行。Agent 和 SubAgent 各自通过 `max_rounds` 和 `cancelled_` 信号保障安全。

**涉及文件**：`ToolCallLoop.h` / `ToolCallLoop.cpp` / `Agent.cpp` / `SubAgent.cpp`
