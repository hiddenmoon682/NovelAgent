# NovelAgent 审查状态总览

> 最后更新：2026-08-17（文档清理：大部分评审/计划原始文档已删除，仅保留本总览作为汇总记录）

---

**当前审查状态**：所有已注册审查批次均已完成或确认无需修复。Serial Tool Call 路径两轮共 30 项发现已全部清零。

> 注：截至 2026-08-17，本文件引用的各评审/计划原始文档（ARCHITECTURE_REVIEW、REVIEW_FALSE_POSITIVES、
> CLI_REMOVAL_PLAN、BOOTSTRAP_SLIM_PLAN、SERIAL_TOOL_CALL_REVIEW_PHASE2、CONCURRENT_INPUT_HANDLING、
> QUANTCLAW_REFERENCE_REVIEW、SIGINT_HANDLING_REVIEW、MULTI_SESSION_PARALLEL_REVIEW/REFERENCE 等）
> 均已完成使命或在结论落地后删除，git 历史可完整追溯；此处仅保留各批次结论摘要。

---

## 〇、多会话并行架构评审 — ✅ 已实施

**目标**：支持「后台会话继续运行」，将单实例 Agent 重构为「每会话一个运行时」。

**状态**：✅ 已实施（2026-08 上旬落地：SessionRuntime 池 + 并发保护 + GUI 多会话，并完成
析构 UAF / busy 聚合 / 并发上限原子化等修复；原评审文档与实施蓝图已删除）。

**核心结论**：
- 方向正确：须将「会话运行时状态」（memory/state/tools/client）从 Agent 单例拆出，改为每会话一份。
- 关键约束：`LLMClient` 单实例不安全（httplib 内部状态不可共享），注释明确要求多线程用 `LLMClientFactory` 每上下文创建独立实例 —— 恰好支持每会话独立 client。
- 建议分阶段实施（阶段 0→5），每阶段独立回归。

---

---

## 一、代码审查（DESIGN_REVIEW_CODE.md）— 28 项，已清零

| 状态 | 数量 |
|------|------|
| ✅ 已修复 | 20 |
| 📋 延后（非 bug） | 1（Issue 12: 模型序列化分离，PCH 已覆盖编译开销） |
| ❌ 不属实 | 1（Issue 20: A17 已修复） |
| ~~误判/夸大~~ | 6（原始记录已删除，见顶部备注） |

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

## 三、CLI 代码删除方案（CLI_REMOVAL_PLAN_2026-07-27.md）

| 状态 | 说明 |
|------|------|
| ✅ 已执行 | 方案 A（彻底删除）已落地，CLI 相关文件已移除；原方案文档已删除 |

### 方案概要
- **A（彻底删除）**：删 12 文件，改 5 文件，失去 `-e` 和 `--cli` 能力
- **B（保留接口）**：删 10 文件，留 IOutputChannel 抽象
- **C（仅移除 REPL）**：删 6 文件，保留输出基础设施和 `-e`/`--cli`

### 建议
推荐方案 A（彻底删除）。理由：Git 历史可完整恢复，无需为"可能"保留死代码。

---

## 四、Bootstrap.h 瘦身方案（BOOTSTRAP_SLIM_PLAN_2026-07-27.md）

| 状态 | 说明 |
|------|------|
| ✅ 已执行 | 激进方案落地：Bootstrap.h 瘦身为 SIGINT 注册 + 构造入口，配置/QML 初始化迁至 QmlBridge；原方案文档已删除 |

### 核心思想
- Bootstrap.h 瘦到只剩构造 NovelAgentApp + SIGINT 注册
- 所有配置迁移到 QML 前端，实现"配置一次，永久生效"
- 自动记住上次打开的项目、默认 Provider、API Key 等

### 执行阶段
1. **QmlBridge 增强** — 新增 initialize/createProject/openProject 等方法
2. **QML 设置 UI** — SettingsDialog / WelcomeWizard / ProjectPicker
3. **NovelAgentApp 延迟初始化** — 支持先构造后配置
4. **Bootstrap.h 瘦身** — 移除 CLI11/AppConfig/ProjectManager 依赖
5. **细节补充** — 窗口状态持久化、自动加载上次项目

---

## 五、SIGINT 信号处理审查（SIGINT_HANDLING_REVIEW_2026-07-27.md）

| 状态 | 说明 |
|------|------|
| ✅ 已结案 | 当前实现可正常工作，原子标志 + 主循环轮询架构正确；平台 API 层面优化（SetConsoleCtrlHandler/sigaction）优先级低，未执行。原评审文档已删除 |

### 发现摘要
1. `signal()` 跨平台行为不一致，Windows 上推荐 `SetConsoleCtrlHandler`，POSIX 上推荐 `sigaction`
2. `extern "C" inline` 取址作为信号处理函数存在隐患
3. 缺少 `SA_RESTART` 支持，信号中断后系统调用可能返回 `EINTR`
4. **无需修复的核心理由**：原子标志 + 主循环轮询的架构是正确的，改进主要是平台 API 层面的优化

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

B2（无模板并行降级）、C1（硬编码超时）、C4（RewindTo 丢弃 pinned 消息）——均为低优先级设计偏好，
未构成本文件最终结论中的"待修 bug"（原始评审文档已删除，见顶部备注）。

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

---

## 十四·五、ToolCallLoop 首轮 LLM 调用冗余审查（TOOL_CALL_LOOP_DUPLICATE_CALL_REVIEW_2026-07-17）— ✅ 已修复

> 发现日期：2026-07-17 · 修复日期：2026-07-18（架构重构）

**原问题**：`ToolCallLoop::run()` 中存在 **3 处 `client_.chat()` 调用**——首轮独立于循环外，循环底部还有一次，导致每轮两次 LLM 调用。

**当前代码验证**：已整合为单一路径——LLM 调用统一在循环顶部，首轮作为第 0 次迭代自然进入，后续轮次在工具执行后进入下一次迭代。

| 场景 | 前 | 后 |
|------|-----|-----|
| 无工具调用 | 首轮 chat + 进入循环 round=0 空检测 | 循环 round=0 → chat → 空检测 |
| 1 轮工具后完成 | 首轮 chat + round=0 执行工具 + 底部 chat + round=1 空检测 | 循环 round=0 → chat → 执行工具 → round=1 → chat → 空检测 |
| max_rounds | 首轮 chat + max_rounds 次循环 chat | max_rounds+1 次循环 chat |

---

## 十五、串行工具调用流程审查（SERIAL_TOOL_CALL_REVIEW_2026-07-19）— ✅ 已清零

> 发现日期：2026-07-19 · 修复日期：2026-07-19
> 审查范围：`Agent::processSerial()` → `ToolCallLoop::run()` → `ToolPipeline::execute()` → `Conversation::apply()`

| # | 问题 | 严重度 | 修复方式 |
|---|------|--------|----------|
| 1 | max_rounds 退出时 assistant 双重添加 + content 丢失 | HIGH | 区分正常/最大轮次退出路径，内容不移动到对话逻辑 |
| 2 | 取消/循环检测退出丢弃有效响应 | HIGH | 在取消/循环检测前保存 last_response 中有效 tool_calls |
| 3 | pipeline.execute() 异常致孤立 assistant | MED | ToolCallLoop catch 中 popBack() 回滚后再 rethrow |
| 4 | 最终 assistant 消息缺失 reasoning_content | MED | processSerial 回传 LLMResponse 时复制 reasoning_content |
| 5 | compact LLM token 未记录到 TokenTracker | MED | on_round_complete hook 中 recordUsage + 保存/恢复 context_size |
| 6 | TokenCounter 未统计 reasoning_content | MED | `countSingleMessage()` 添加 reasoning_content 统计 |
| 7 | processSerial 忽略退出原因标志 | MED | 取消/循环检测时设置 finish_reason 字段 |
| 8 | 取消检查在 LLM 调用之后 | LOW | ToolCallLoop 首轮前增加 cancelled_ 检查 |
| 9 | max_repeated_calls ≤ 0 无钳位 | LOW | setMaxRepeatedCalls 钳位为  |
| 10 | chat()/hook 抛出同致孤立轮次 | LOW | catch 块 popBack + rethrow，外层兜底 catch 完整回滚 |
| 11 | 异常 catch 返回无法区分的空响应 | LOW | 改为返回含 finish_reason="error" 的 LLMResponse |
| 12 | buildEffectivePrompt 混用成员/参数 | LOW | 统一使用 conversation 参数 |
| 13 | streaming 字段死代码 | CLEANUP | 删除 `StreamCallbacks` 中无用的 streaming 字段 |
| 14 | executeAndAppend 未使用 | CLEANUP | 删除 executeAndAppend 方法 |
| 15 | tool_calls 拷贝非 move 易误导 | CLEANUP | 添加注释说明为何 tool_calls 必须拷贝 |

---

## 十六、TokenTracker 校准与检查机制审查（TOKEN_TRACKER_REVIEW_2026-07-16）— ✅ 已关闭

> 发现日期：2026-07-16 · 关闭日期：2026-07-21

| # | 问题 | 严重度 | 状态 |
|---|------|--------|------|
| 3 | `setCurrentContextSize()` 注释称"供 assemble() 使用"与实际调用者不符 | 低 | ✅ 注释已修正：说明实际消费者是 compact() 的两处调用 |
| 4 | `check()` 无参版（API prompt_tokens）与 `check(realtime)`（启发式×校正因子）计量体系不一致 | 低 | 📋 接受差异：两者处于请求不同生命周期，使用当时最佳数据，校正因子趋近 1.0 时空隙消失 |

**决策说明**：问题 4 采用方案 A（接受差异）。hook 路径使用 API 真实值做快速检查，assemble 路径用估算值做精确预算——设计意图正当，差异可控。

---

## 十七、串行工具调用流程二次审查（SERIAL_TOOL_CALL_REVIEW_2026-07-19_PHASE2）— ✅ 已清零

> 审查日期：2026-07-19 · 修复日期：2026-07-21
> 详细报告：`SERIAL_TOOL_CALL_REVIEW_2026-07-19_PHASE2.md`（已删除，见顶部备注）

| # | 问题 | 严重度 | 验证 | 修复方式 |
|---|------|--------|------|----------|
| 1 | process() 异常不撤销 conversation 修改 | HIGH | CONFIRMED | catch 块回滚 conversation_snapshot + clearCompactedSummary() |
| 2 | Conversation::apply() 部分执行无回滚 | HIGH | CONFIRMED | copy-then-swap 原子 apply，异常不影响原消息 |
| 3 | UTF-8 截断残留不完整 leading byte | HIGH | CONFIRMED | utf8CharTruncatePos 替换 byte 级 resize |
| 4 | isRepeatedCall JSON 键顺序漏检 | HIGH | CONFIRMED | parse→dump 标准化键顺序 |
| 5 | compact 异常清空上次成功摘要元数据 | HIGH | CONFIRMED | compact catch 不清空；外层 process() catch 负责清空 |
| 6 | Token 校准忽略 system prompt | MED | CONFIRMED | estimated 加入 countTokens(system_prompt) |
| 7 | resize 字节限制误作字符限制 | MED | CONFIRMED | utf8CharLen + utf8CharTruncatePos 字符级截断 |
| 8 | rounds_executed 少计 1 轮 | MED | CONFIRMED | 正常和取消退出路径统一设置 rounds_executed |
| 9 | cancelled_ 检查在 chat() 之后 | MED | CONFIRMED | 首轮前增加 cancelled_ 检查，避免浪费 API 调用 |
| 10 | hook 硬编码 conversation_ | MED | CONFIRMED | lambda 捕获 &conversation 参数 |
| 11 | 取消路径不设 rounds_executed | LOW | CONFIRMED | 首轮前取消失真前设置 rounds_executed=0 |
| 12 | std::set 固定集合效率 | LOW | PLAUSIBLE | 改为 constexpr std::array + std::ranges::find |
| 13 | kCompactKeepExchanges 注释过期 | LOW | CONFIRMED | 注释更新为实际值，常量调至 5（保留 10 条消息） |
| 14 | rewindTo 窄化转换溢出 | LOW | PLAUSIBLE | 比较反转：size_t 侧 static_cast 代替 int 侧 |
| 15 | truncateResult 死代码 | CLEANUP | CONFIRMED | 删除函数声明和实现（零调用者） |

---

## 十八、Qt 并发输入处理参考（CONCURRENT_INPUT_HANDLING_2026-07-19）— 设计参考

> 记录日期：2026-07-19 · 非 Bug，为 Qt 前端并发输入的架构参考（原文档已删除，见顶部备注）
> 问题：上一条任务还在执行时用户输入了新指令，系统应如何处理

**推荐方案**：取消当前任务（利用已有的 `cancelled_` 标志 + 终止记录），然后处理新输入。需要改造 BackendServer 的会话锁和 Agent 的状态守卫。

**实际走向**：未采用方案 A（取消），最终以方案 C（多会话并行架构，见§〇）落地——每会话独立运行时并发执行，无需中断他会话任务。
