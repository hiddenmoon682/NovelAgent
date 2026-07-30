# Changelog

## [2026-07-30] 移除 Shell/PowerShell 工具

### 删除 — ShellTools（YAGNI + 安全面收缩）
- 删除 `src/agent/tools/ShellTools.h/.cpp`（`run_powershell` 工具）与 `tests/test_shell_tools.cpp`；
  工具靠 `REGISTER_TOOL_NP` 自注册，源文件删除即自动从注册表移除，无需改装配/注册代码。
- 理由：纯小说创作 Agent 用不到 Shell 执行能力，删除后减少提示注入下的安全面；
  同时消除独占 ~161.6s 的 test_shell_tools 慢测试（ctest 全量墙钟从 ~167s 降至 10s 量级）。
- 同步清理：`cmake/Sources.cmake`、`tests/CMakeLists.txt`（两处测试列表）、`CLAUDE.md`（安全规则/RAII 示例）。

## [2026-07-27] 真正的多会话 + Token 用量展示

### 新增功能 — 多会话存储与编排
- `SessionPersistence.h/.cpp`：全量重写为多会话布局 `.novelagent/sessions/index.json`（active + 会话元信息列表）+ `sessions/<id>.json`（消息数组）；
  新增 `SessionInfo` 结构体和 `listSessions()/activeSessionId()/createSession()/switchSession()/deleteSession()` API；
  首次 save 时从首条 user 消息提取自动标题（UTF-8 安全截断 30 字节）；
  删除会话时非空内容归档到 `archive/<id>.json`，删除 active 会话自动切到最近更新的剩余会话。
- `Agent.h/.cpp`：新增 `switchSession()/deleteSession()` 会话编排（先保存当前会话再切换/重载）；
  `resetSession()` 改为多会话语义（保存当前 + 新建空会话，当前会话为空时不新建避免堆积）。

### 新增功能 — Token 用量展示
- `Agent.h/.cpp`：新增 `ContextUsage` 缓存（total_tokens + percent），在 `process()` 成功、会话加载/切换/重置后通过 `refreshUsage()` 调用 ContextBudgetEvaluator 刷新。
- `NovelAgentApp.cpp`：TokenBudget 注入提前到 `loadSessionState()` 之前，启动恢复时百分比使用真实模型上限。
- `QmlBridge.h/.cpp`：`totalTokens/contextPercent` 属性接真实数据；新增 `sessionList()/switchSession()/deleteSession()` Q_INVOKABLE 和 `sessionsChanged` 信号。
- `SidebarPanel.qml`：会话列表从硬编码占位改为真实数据（整行点击切换、悬停删除按钮，HoverHandler 行高亮）。
- `AgentPanel.qml`：`onSessionReset` 改为 `reloadHistory()`，切换会话后加载目标会话历史。

### 重构 — 旧单会话 API 清理（YAGNI）
- `ProjectIO.h/.cpp`：删除 `loadConversation/appendConversation/saveConversation` 及 `kConversationJson` 常量；`createProjectDir` 不再预建 `conversation.json`。
- 旧单会话格式不做兼容迁移（当前阶段无存量用户数据需要兼容）。

### 测试
- `test_agent.cpp`：B2 重写为新 sessions 布局验证，新增 `contextUsage().total_tokens > 0` 断言。
- `test_context_manager.cpp`：新增 `test_multi_session_lifecycle`（新建/切换/删除/归档往返）。
- `test_project_io.cpp`：删除已无 API 对应的 `test_conversation`。
- 全量：23/23 通过（排除 deepseek|shell）。

## [2026-07-23] 删除并行编排相关代码

### 重构 — 移除并行处理路径
- `Agent.h`：删除 `useParallelProcessor()` / `isParallelEnabled()` / `parallel_mode_` / `processParallel()` 声明；
  删除 `#include "agent/AgentOrchestrator.h"` 和 `class TemplateManager;` 前向声明；
  清理 `orchestrator_` 成员。`processSerial()` 为唯一私有处理路径。
- `Agent.cpp`：删除 `initOrchestrator()`（原 `useParallelProcessor()`）；
  删除 `processParallel()` 函数；
  删除 `orchestrator_.reset()` 和 `parallel_mode_` 初始化；
  `process()` 中删除 if-else 分支，始终调用 `processSerial()`。
- `ReplHandler.cpp`：删除 `/parallel on|off` 命令；状态栏不再显示模式开关。
- `CHANGELOG.md`：追加本次记录。

## [2026-07-20] 用户取消机制实现（修复 #9 #11 + SSE 流式取消）

### Bug 修复
- `ToolCallLoop.cpp`：修复 #9 — `cancelled_` 检查移到循环开始处，不浪费 LLM API 调用；`chat()` 传入 `cancel_flag`；新增 `chat()` 返回后的二次检查。
- `ToolCallLoop.cpp`：修复 #11 — 取消退出路径正确设置 `rounds_executed`。
- `ReplHandler.cpp`：修复 Ctrl+C 在 `std::getline` 中导致 REPL 退出的问题，`clear()` 后继续循环。

### 新增功能 — 用户取消机制
- `ILLMClient.h` / `LLMClient.h`：`chat()` 新增 `const std::atomic<bool>* cancel_flag` 可选参数。
- `LLMClient.cpp`：SSE 流式回调中检查 `cancel_flag`，收到取消信号时 `return false` 中止 HTTP 连接，返回已累积的部分响应。
- `Agent.h`：新增 `cancel_requested_` 原子标志成员 + `requestCancel()` / `cancelFlag()` / `resetCancel()` 方法。
- `Agent.cpp`：`processSerial()` 中 `ToolCallLoop::setCancelled(&cancel_requested_)` 接入取消信号。
- `main.cpp`：注册 `SIGINT` 信号处理器，在 `runRepl()/runExec()` 期间将 `g_cancel_flag` 指向 Agent 的取消标志。
- `ReplHandler.cpp`：处理 `std::cin` 在 Ctrl+C 后的 fail 状态，显示取消提示。

### 测试适配
- `test_tool_call_loop.cpp` / `test_context_manager.cpp` / `test_sub_agent.cpp`：Mock `chat()` 签名新增 `const std::atomic<bool>*` 默认参数。

## [2026-07-20] 新增 QuantClaw 参考审查、消息队列计划

### 文档
- `docs/review/QUANTCLAW_REFERENCE_REVIEW_2026-07-20.md`：新增 QuantClaw 参考项目审查，记录两个发现：
  - **缺乏上下文溢出恢复**：LLM 返回 context overflow 时无重试机制，对比 QuantClaw 的 `CompactOverflow()` + 最多 3 次重试
  - **流式模式工具执行时机**：QuantClaw 在 stream callback 中收到 tool_call chunk 时即执行工具，不等流结束
- `docs/plan/QUEUE_QT_INTEGRATION_PLAN_2026-07-20.md`：新增消息队列 + Qt 前端集成计划
- `docs/review/TOOLCHAIN_AND_PARAMETER_VALIDATION_2026-07-20.md`：新增 ToolChain 功能记录 + LLM 参数传错问题分析

## [2026-07-19] 完整修复串行工具调用流程 15 个发现（#6-#15）

### Bug 修复
- `ContextManager.cpp`：compact LLM 调用的 token 消耗计入 TokenTracker（`tracker_.record()`），压缩后更新上下文快照为压缩后的新对话大小。
- `TokenCounter.cpp`：`countSingleMessage()` 和 `countMessages()` 新增 `reasoning_content` 字段的 token 统计。
- `Agent.cpp`：processSerial 中 `ToolCallLoopResult` 的 `cancelled`/`loop_detected` 标志传播到 `LLMResponse::finish_reason`。
- `ToolCallLoop.cpp`：`chat()` 调用和 `on_round_complete` hook 包裹 try-catch，异常时记录日志并重新抛出。修复 #10。
- `Agent.cpp`：`process()` 和 `execute()` 的 6 个提前返回路径（空输入/校验失败/状态拒绝/异常）均设置 `finish_reason`，调用方可区分错误类型。修复 #11。
- `Agent.cpp`：`buildEffectivePrompt()` 使用形参 `conversation` 而非成员 `conversation_`。修复 #12。

### 源码清理 & 优化
- `ToolCallLoop.h`：删除未使用的 `streaming` 字段和 `setStreaming()` setter。修复 #13。
- `ToolPipeline.h/.cpp`：删除 `executeAndAppend()`（无调用点）及其关联的 `conversation_` 成员和双参数构造函数。修复 #14。
- `ToolCallLoop.cpp`：`addAssistantFromResponse()` 的 tool_calls 拷贝处加警告注释，防止未来误改为 move。修复 #15。

### Bug 修复
- `ToolCallLoop.cpp`：最后一轮调用 chat() 时移除工具定义并追加提示词，避免 LLM 再请求工具调用无法结束。修复 #1（max_rounds 退出时 assistant 双重添加 + content 丢失）。
- `ToolCallLoop.cpp`：取消路径将本轮响应加入对话（有 tool_calls 时追加终止结果），后续 LLM 可见任务已被取消。零额外 token 开销。
- `ToolCallLoop.cpp`：正常路径中 `pipeline.execute()` 加 try-catch，异常时 `popBack()` 回滚已添加的孤立 assistant 消息。修复 #3。
- `Agent.cpp`：processSerial 最终 assistant 消息补全 `reasoning_content` 复制。修复 #4。
- `ToolCallLoop.cpp`：循环检测路径改为先加入 assistant(tool_calls) + 终止结果到对话（保证消息序列合法），再发一轮 chat() 通知 LLM 重复情况并获取最终文字答复。修复 #2（取消/循环检测退出丢弃有效响应）。

> 不再清理任何 assistant 消息的 reasoning_content（思考过程）。理由：① 实现复杂度归零；
> ② 保留推理过程不影响模型回复质量（实测保留时回复更连贯）；③ token 成本可忽略
> （~几百 token/轮，对比 1M 窗口九牛一毛）；④ 消除"何时/怎样 strip"的 bug 隐患。

### 源码清理 & 优化
- `Conversation.h`：删除 `stripReasoningContent()` 方法
- `Agent.cpp`：删除 `conversation.stripReasoningContent()` 调用及其注释
- `ContextManager.cpp`：压缩时在 compact prompt 中附带 `[思考过程]` 内容，避免摘要丢失推理中的关键信息
- `tests/test_e2e_reasoning_strip.cpp`：删除（专用测试不再需要）

### 行为变化
- 所有 assistant 消息的 `reasoning_content` 永久保留在对话中
- 不再做条件判断（有/无 tool_calls 均不处理）
- 与 DeepSeek API 规范一致（reasoning_content 回传不会导致 400）

## [2026-07-18] 删除反思（Reflection）机制

> REFLECTION_MECHANISM_REVIEW: ToolCallLoop 中的反思机制名不副实——检测到重复调用后仅注入模板消息就重新调 LLM，跳过工具执行、没有实际错误分析。每轮反思浪费一次 LLM 调用但无新信息，安全网由 `loop_detected` 终止保障。

### 源码清理
- `ToolCallLoop.h`：删除 `reflection_rounds_` 成员、`max_reflection_rounds` 字段+setter、`buildReflectionPrompt()` 声明、全部 `CRIT-2` 注释
- `ToolCallLoop.cpp`：删除 `buildReflectionPrompt()` 方法体；`has_repeated` 分支简化为直接 `loop_detected` 终止；删除 `reflection_rounds_ = 0` 重置和 `CRIT-2` 注释
- `ExecutionTracer.h`：删除 `ReflectionPayload` 结构体和 TracePayload variant 中条目（从未被任何代码 record）
- `ExecutionTracer.cpp`：删除 `ReflectionPayload` 序列化分支
- `test_tool_call_loop.cpp`：删除 `test_repeated_call_reflection()` 和 `test_reflection_exhausted()` 两个测试，更新文件头注释

### 行为变化
- 工具重复调用不再进入"反思→重试"循环，而是直接以 `loop_detected` 终止
- `isRepeatedCall()` 重复检测逻辑保留，仍可识别并终止死循环

## [2026-07-16] 删除 IMessageProcessor 模块，内联串行/并行处理逻辑到 Agent

> IMessageProcessor 策略模式存在 8 个属性与 Agent 完全重叠，6 个 setter 方法仅做转发
> 胶水，配置变更需四级传递。删除抽象层后架构更扁平，消除属性重复和配置传播代码。

### 架构变更
- **删除** `IMessageProcessor.h` / `IMessageProcessor.cpp`（~513 行）
- `Agent.h`：移除 `processor_` 成员和 `setProcessor()` 方法；新增 `parallel_mode_` 标志和
  `orchestrator_` 成员；新增 `processSerial()` / `processParallel()` / `buildEffectivePrompt()`
  私有方法
- `Agent.cpp`：`processUserMessage()` 改为根据 `parallel_mode_` 标志 if-else 选择处理路径；
  内联原 SerialProcessor::process()、ParallelProcessor::process()、buildEffectivePrompt()
  实现；消除全部配置传播代码（setSystemPrompt/setMaxToolRounds/setContextManager/
  setMaxContextTokens 不再同步到 processor）

### 源码清理
- `cmake/Sources.cmake`：删除 IMessageProcessor 构建条目
- `ReplHandler.cpp`：删除 `/config max_context_tokens` 中通过重建 processor 同步配置的 hack
- `SessionManager.cpp`：删除冗余的 `useSerialProcessor()` 调用（Agent 构造函数已默认串行模式）
- `NovelAgentApp.cpp`：更新构造注释

## [2026-07-16] 重命名 effective_prompt → effective_system_prompt

> NAMING_ISSUES_REVIEW: `effective_prompt` 命名歧义已修复，明确其角色为"系统提示词"。

### 源码清理
- `IMessageProcessor.cpp`：`SerialProcessor::process()` 和 `ParallelProcessor::process()` 中重命名
- `Agent.cpp`：`Agent::execute()` 中重命名

## [2026-07-16] 删除 ToolCallLoop 中所有 tracer 记录

> 删除 ToolCallLoop 中全部的 tracer_->record() 调用、tracer_ 成员变量、
> 构造函数参数，以及相关的 ErrorPayload/ReflectionPayload/ToolCallPayload 引用。

### 源码清理
- `ToolCallLoop.h`：删除 `#include ExecutionTracer.h`、tracer 构造函数参数、tracer_ 成员
- `ToolCallLoop.cpp`：删除全部 10 处 tracer_->record() 调用
- `IMessageProcessor.cpp`：更新 ToolCallLoop 构造调用（去掉 tracer 参数）
- `SubAgent.cpp`：更新 ToolCallLoop 构造调用（去掉 tracer 参数）

## [2026-07-16] 删除 ToolCallLoop 中不必要的计时代码

> 移除 `ToolCallLoop::run()` 内部全部 8 次 `steady_clock::now()` 调用，
> round_ms/tool_ms 计时仅 tracer 一个消费者，无 tracer 时完全浪费。

### 源码清理
- `ToolCallLoop.cpp`：删除首轮、正常循环路径、反思路径三处的计时代码和 round_ms/tool_ms 计算
- `ToolCallLoop.cpp`：删除冗余的 `#include <chrono>`

## [2026-07-16] 移除 `initial_messages` 参数

> 删除 `ToolCallLoop::run()` 的 `initial_messages` 参数及相关代码，此功能已因预思考代码清理和 ContextManager 直接修改 conversation 而不再需要。

### 源码清理
- `ToolCallLoop.h`：从 `run()` 签名中移除 `initial_messages` 参数
- `ToolCallLoop.cpp`：删除三元表达式，首轮直接使用 `conversation.messages()`
- `IMessageProcessor.h`：`buildEffectivePrompt()` 移除 `out_messages` 传出参数
- `IMessageProcessor.cpp`：删除 `effective_messages` 局部变量，重构 `buildEffectivePrompt()` 签名

## [2026-07-16] 清理预思考代码（A4 use_thinking_step）

> 删除 `use_thinking_step` 字段及相关代码块，清理关联 review 文档，为 Plan Mode 从零设计扫清障碍。

### 源码清理
- `ToolCallLoop.h`：删除 `use_thinking_step` 字段及注释
- `ToolCallLoop.cpp`：删除预思考步骤代码块（A4 ReAct 思考阶段）

### 文档同步
- `docs/design/plan_mode.md`：标记为 obsolete（旧代码已清理，待重写）
- `docs/review/REVIEW_STATUS.md`：A4 行更新为"已清理"
- `docs/review/INITIAL_MESSAGES_REVIEW_2026-07-16.md`：更新预思考相关引用
- `docs/review/PLAN_MODE_CLEANUP_PLAN.md`：内容已合并到 REVIEW_STATUS.md，文件删除

## [2026-07-16] 架构审查文档整理 + 注释修正

> 系统提示词所有权审查、initial_messages 参数审查、命名问题审查等三份文档写入 `docs/review/`。
> 将 `PLAN_MODE_CLEANUP_PLAN.md` 从 design 移至 review。
> 更新 `IMessageProcessor.h` 中 ContextManager 过时注释（移除"RAG 检索"引用）。

### 新增审查文档
- `docs/review/SYSTEM_PROMPT_OWNERSHIP_REVIEW_2026-07-16.md`：系统提示词三副本问题分析与 Conversation 统一管理决议
- `docs/review/INITIAL_MESSAGES_REVIEW_2026-07-16.md`：`ToolCallLoop::initial_messages` 参数陈旧快照、死代码等问题分析
- `docs/review/NAMING_ISSUES_REVIEW_2026-07-16.md`：`effective_prompt` 命名歧义分析与修复建议

### 文档整理
- `docs/design/PLAN_MODE_CLEANUP_PLAN.md` → `docs/review/PLAN_MODE_CLEANUP_PLAN.md`（按 review 分类归档）
- 删除 `docs/design/thinking_step_detector.md`（已纳入 plan_mode.md 不再独立维护）

### 注释修正
- `IMessageProcessor.h`：ContextManager 成员注释更新，移除过时"RAG 检索"描述，改为准确职责：动态 system prompt / Token 追踪 / 对话压缩 / 会话持久化

## [2026-07-16] 修复工具注册缺失 + 清理架构审查文档

> 修复 4 个工具缺少 REGISTER_TOOL 宏导致的静默失效（LLM 被指导使用却永远收到"工具未找到"）。
> 更新架构审查文档，已修复项移至新文件 ARCHITECTURE_REVIEW_RESOLVED.md，仅保留 3 项待处理。
> 删除过时分析文档：MAYBE_AUTO_COMPACT.md（全索引模式已解决）、TOOL_REGISTRATION_GAP.md（已修复）。
> 新增设计文档：Plan Mode 用户可控预思考步骤、A4 条件化 Thinking Step Detector。

### Bug 修复
- `ChapterContextTools.cpp` / `RelevantCharacterTools.cpp` / `RelevantSettingTools.cpp` / `RelevantWorldRuleTools.cpp`：4 个工具补充 `REGISTER_TOOL` 宏，消除编译有定义但运行时不可用的静默失效

### 文档清理
- `ARCHITECTURE_REVIEW_2026-06-30.md`：精简为仅含 B2/C1/C4 三项待处理问题，其余已修复/已关闭
- `REVIEW_STATUS.md`：新增第六节（架构审查 11 项分类统计）、第七节（工具注册缺失审查结论）
- 删除 `MAYBE_AUTO_COMPACT.md`（章节切换自动压缩设计问题，已通过全索引模式解决）
- 删除 `TOOL_REGISTRATION_GAP.md`（工具注册缺失记录，已修复）

### 代码微调
- `ToolCallLoop.cpp`：注释缩进对齐
- `Conversation.h`：`pinned_indices` 注释修正为明确索引范围为 `diff.added` 内部

### 新增设计文档
- `docs/design/plan_mode.md`：Plan Mode 用户可控预思考步骤设计（状态：待审查）
- `docs/design/thinking_step_detector.md`：A4 条件化 Thinking Step Detector 重构设计（状态：待审查）

## [2026-07-15] 修复 DeepSeek reasoning_content 丢失问题 + 可配置 Thinking 模式

> 修复了三处 reasoning_content 丢失问题：(1) Message 结构体缺少字段，
> (2) ToolCallLoop 不复制 reasoning_content，(3) buildRequestBody 未请求思考模式。
> 新增 ProviderConfig 中 enable_thinking / reasoning_effort 可配置项。

### 核心改动
- `Message.h`：新增 `reasoning_content` 字段及 to_json/from_json 序列化
- `ToolCallLoop.cpp`：带 tool_calls 路径下复制 reasoning_content 到 assistant 消息
- `AppConfig.h`：ProviderConfig 新增 `enable_thinking`（默认 false）+ `reasoning_effort`（默认 "high"）
- `LLMClient.cpp`：buildRequestBody 中根据 enable_thinking 添加 thinking 参数
- `test_app_config.cpp`：新增 3 个 thinking 配置测试
- `test_tool_call_loop.cpp`：新增 reasoning_content 保留测试

### 设计决策
- reasoning_content 不持久化到 conversation.json（仅内存保留）
- enable_thinking 默认关闭（opt-in，避免 token 浪费）
- reasoning_effort 默认 "high"

## [2026-07-13] 工具自注册宏灵活化：新增 REGISTER_TOOL_NP 消除手动样板代码

> 新增 `REGISTER_TOOL_NP` 宏，用于不需要 `Project` 指针的工具注册。
> 将 `ShellTools.cpp` 和 `SearchMemoryTools.cpp` 中的手动注册块替换为单行宏调用。

### 核心改动
- `BuiltInTool.h`：新增 `REGISTER_TOOL_NP(ToolClass, toolName, varSuffix)` 宏，工厂 lambda 构造工具时不传 Project 参数
- `ShellTools.cpp`：手动注册（9 行）→ `REGISTER_TOOL_NP`（1 行）
- `SearchMemoryTools.cpp`：手动注册（9 行）→ `REGISTER_TOOL_NP`（1 行）
- `CLAUDE.md`：更新工具自注册章节，说明两个宏的适用场景
- `SearchMemoryTools.h`：注释更新

### 影响范围
- 34 处现有 `REGISTER_TOOL(...)` 零改动
- `BuiltInTool::Factory` 签名不变，`registerAllTo` 不变
- 净删 ~14 行样板代码，净增 ~12 行宏定义

## [2026-07-12] 合并 ContextManager::assemble() 自动压缩和告警为多级决策 + 四级阈值体系

> 将 assemble() 中步骤 2（自动压缩检查）与步骤 3（阈值告警）合并为统一的多级决策块，
> 用 checkThresholds() 作为单一入口，消除重复计算和压缩成功后仍产生误导性告警的问题。
>
> 引入 Error/AutoCompact 状态，四级可配置阈值体系：Warning(60%) → Critical(85%) → AutoCompact(95%) → Error(>100%)。
> 阈值从硬编码改为可配置，auto_compact_threshold_ 从 ContextManager 移至 TokenTracker 统一管理。

### 核心改动
- `ContextManager::assemble()`：合并步骤 2+3；自动压缩条件改为 `pre_check.status >= AutoCompact`；四级 switch 告警
- `ContextStatus` 枚举：`Normal/Warning/Critical` → `Normal/Warning/Critical/AutoCompact/Error`
- `ContextAssembly`：新增 `bool fatal` 标志，Error 状态下置位供调用方中断请求
- `TokenTracker`：新增三个可配置阈值字段（warning/critical/auto_compact）+ setter/getter；`check()`/`check(int)` 四级判定替代硬编码 60/85
- `ContextManager`：删除 `auto_compact_threshold_` 字段（阈值归属 TokenTracker）；`setAutoCompact` 默认值 70→95；新增 `setWarningThreshold`/`setCriticalThreshold`/`setAutoCompactThreshold` 转发接口

### 注释同步
- `ContextManager.h`：assemble() 处理流程由 5 步改为 4 步
- `ContextManager.cpp`：assemble() 流程注释块同步更新为 4 步结构

### 测试
- `test_critical_warning`：从搜索 "接近模型上限"（Critical）改为 "超过模型上限"（Error）+ 验证 `result.fatal`

## [2026-07-11] 合并 Compactor 到 ContextManager + 修正自动压缩触发时机

> Compactor 类展开合并到 ContextManager，消除 1:1 转发方法和重复字段同步代码。
> 自动压缩检查从 Agent（请求前，读陈旧数据）移到 assemble() 内部（实时 total_tokens），
> 解决新用户输入可能导致超阈值但检查过早无法捕获的问题。

### 核心改动
- `ContextManager`：合并 Compactor 全部成员和方法（`summary_`, `marker_`, `auto_compact_`, `compact()` 等），删除 `compactor_` 转发层
- `ContextManager::assemble()`：新增步骤 2.5 自动压缩检查，基于本轮实时 `total_tokens` 判断，修正了此前用 `tracker_.usagePercent()`（上一轮陈旧数据）的问题
- `ContextManager::assemble()` 签名：`const Conversation&` → `Conversation&`（压缩需修改对话），新增 `ILLMClient*` 参数（默认 nullptr）
- `Agent::processInput()`：删除步骤 4 早期自动压缩检查（已由 assemble 内部接管）
- `setCalibrator`/`setModelName`/`setModelContextLimit`：删除 `compactor_.setXxx()` 同步调用，字段只存一份

### 删除的文件
- `src/agent/Compactor.h`、`src/agent/Compactor.cpp`：合并到 ContextManager
- `tests/test_compactor.cpp`：核心测试已叠加覆盖

### 消除的冗余
- `kCompactKeepExchanges` / `kCompactSystemPrompt`：两份重复常量合并为一份
- `calibrator_` / `model_name_` / `model_context_limit_`：不再在 Compactor 中重复存储
- 9 个 1:1 转发方法：变为直接成员访问

## [2026-07-11] 删除 truncateMessages 截断机制

> 截断丢弃旧消息永久丢失信息，与 compaction（摘要保留）的哲学相悖。
> `shouldAutoCompact()` 在 70% 阈值主动压缩，截断安全网几乎不会触发。

### 核心改动
- `ContextManager::assemble()`：删除步骤 3 截断，全部消息直接通过
- `ContextManager`：删除 `truncateMessages()`、`lastTruncatedCount()`、`truncated_count`
- `IMessageProcessor`：删除依赖 `lastTruncatedCount()` 的同步压缩逃生阀
- 测试：移除 6 个截断相关用例

### 文档
- `COMPACTOR_PROJECT_REF_TRUNCATION.md` 状态更新：9/12 已解决（原 8/12）

## [2026-07-11] 重构：压缩摘要从 system prompt 迁移到对话消息

> 将压缩摘要从 `assemble()` 步骤 1 中注入 system prompt 改为在 `compact()` 时
> 以 user/assistant 消息对直接插入对话列表头部。解决三个问题：
> 1. 语义错位 — 摘要是"事实"而非"指令"，不应放在 system prompt 中
> 2. API 缓存 — system prompt 不再因摘要变化而失效
> 3. 永久累积 — 旧摘要现在在消息列表中，下次 compact() 能被重新压缩

### 核心改动
- `Compactor::compact()`：删除旧消息后插入 `addUser("【系统】以下是被压缩的旧对话摘要：")` + `addAssistant("[被压缩的历史摘要]\n" + 摘要)`
- `ContextManager::assemble()`：删除步骤 1 摘要注入代码（不再读 `compactor_.summary()`），重编号步骤 1-6
- `ContextManagerTypes.h`：删除 `ContextAssembly::has_compacted_context`（无消费者）
- `kCompactSystemPrompt`：增加旧摘要提示，防止多次压缩信息衰减

### 文档
- `COMPACTOR_PROJECT_REF_TRUNCATION.md` 状态更新：8/12 已解决（原 7/12）

## [2026-07-11] Phase 3.7 补充：删除 assemble() 自动向量检索

> 删除 `ContextManager::assemble()` 中每轮自动执行向量检索并注入 system prompt 的逻辑。
> 理由与该阶段已删除的 `buildProjectRef` / `maybeAutoCompact` 相同：代码替 LLM 做决定——不可控、
> 质量不高、导致 system prompt 膨胀。LLM 通过 `search_memory` 工具按需搜索，结果在对话消息中而非 system prompt。

### 代码清理
- `ContextManager::assemble()` 删除步骤 1 自动向量检索块（~60 行）
- 删除 `setRetrievalBackend` / `isVectorStoreStale` / `clearVectorStore` / `hasRetrievalBackend` 接口
- 删除 `vector_store_` / `embedding_gen_` / `retrieval_top_k_` / `vector_store_dirty_` 成员变量
- 删除 `SessionMeta::vector_store_dirty` / `ContextAssembly::has_semantic_context` 字段
- 删除 `Agent::rewindTo` 中对 `clearVectorStore` 的调用
- 删除 `test_isVectorStoreStale_reads_novel_json` 测试（`test_context_manager` 23→22 测试）

### 文档
- `COMPACTOR_PROJECT_REF_TRUNCATION.md` 状态更新：7/12 已解决（原 6/12）

## [2026-07-10] 注释清理：移除 Doxygen 标记

> 移除所有 `@param`、`@return`、`@note`、`@warning`、`@brief`、`@throws` 等 Doxygen 标记，
> 与已统一为 `//` 的注释风格保持一致。

### 代码清理
- 31 个文件中的 `@param`/`@return`/`@note`/`@warning`/`@brief`/`@throws` 标记已移除

## [2026-07-10] 注释风格统一：/// → //

> 将项目中所有 `///` 注释统一替换为 `//`，消除 Doxygen 风格的多斜杠注释。

### 代码清理
- 批量替换 `src/` 和 `tests/` 下全部 100 个 .h/.cpp 文件中的 `///` 为 `//`
- 涉及注释风格包括：文件头注释、函数说明、参数注释、行内 `///<` 注释等

## [2026-07-10] 注释清理：移除 //< 中的 < 符号

> 清除行内注释中残留的 `<` 符号，统一注释风格。

### 代码清理
- 15 个文件中的 `//<` 替换为 `//`

## [2026-06-30] 审查修复补充：测试全覆盖 + DirtyBit 防护 + Shell 扩展

> 基于设计审查修复评估报告，按优先级完成全部遗留修复项。
> 全量 22/22 通过。

### 测试覆盖（新增 4 个测试套件 + 22 个用例）
- `test_setting_tools.cpp`（6 用例）：create/get/get_all/update/delete+级联/错误处理
- `test_world_rule_tools.cpp`（5 用例）：create/get/update/delete+级联/错误处理
- `test_outline_tools.cpp`（7 用例）：get/create_volume/update_volume/create_plot_thread/update_plot_thread/get_project_status/错误处理
- `test_style_tools.cpp`（5 用例）：read_default/update_string/update_int/update_array/空fields错误
- `test_shell_tools.cpp`：新增扩展白名单测试（Get-Process/Get-Service/Get-Acl/Get-Member/Write-Output/别名）

### DirtyBit 防护（Issue 5 安全加固）
- `ProjectIO::save()`：dirty_flags==0 但有子实体时全量保存 + 告警，防止新增工具漏调 markDirty()

### ShellTools 白名单扩展
- 新增 `get-process`/`get-service`/`get-itemproperty`/`get-acl`/`get-member`/`write-output`/`write-host` 及别名（`ps`/`echo`/`gp`/`gl`）

### StyleTools 修正
- `UpdateStyleTool::parameters()`：空 fields schema → 完整列出 22 个可更新字段（C5 修复遗漏）
- 修复由此导致的 C4 校验误阻断 LLM 传入合法字段

### 代码清理
- `ChapterTools.cpp`：删除 `AppendChapterTool` 中重复的 return 语句（死代码）

> 对 DESIGN_REVIEW.md 全部 35 项已修复条目做逐项源码复核（三路并行核实 + 人工二次确认）。
> 复核结论：核心修复（B1/B2/B3/B8/A1/A2/A4/A5/A7/A8/D2/A15 等）均已实质到位且有测试；
> 发现 3 项遗留短板并直接修复，新增 1 个测试可执行文件 + 3 个测试用例。
> 全量 17/17 通过（test_shell_tools 为本次新增）。

### A6 — 跨实体引用校验覆盖补齐（部分修复→修复）
- 复核发现评审 A6 点名的 `Relationship.target_character_id` 在 `update_character_relationships` 中未校验；
  `update_chapter_scenes` 的 pov_character_id/participants/location_id/plot_thread_ids、
  `update_volume` 的 start/end_chapter_id/focus_characters/active_plot_threads 均未校验。
- `CharacterTools.cpp`：`update_character_relationships` 补 target_character_id 软校验
- `ChapterTools.cpp`：新增 `validateSceneRefs` helper，`update_chapter_scenes` 解析每个场景后校验 4 类引用
- `OutlineTools.cpp`：`update_volume` 字段循环补 start/end_chapter_id + focus_characters + active_plot_threads 校验
- 顺带清理 ChapterTools.cpp 未使用的 `validateChapterId`/`validatePlotThreadId`（消除 -Wunused-function 警告）

### C1+C2 — Shell 白名单绕过收紧 + 段内参数解析 bug 修复
- 复核发现 `foreach-object` 在白名单中可执行脚本块 `{ ... }`，且注入拦截未覆盖 `{}`/`;`/`&`
- `ShellTools.cpp`：移除 `foreach-object`；入口拦截 `{` `}` `;` `&` 四类脚本注入字符
- **修复预存 bug**：原 token 解析把段内参数（如 `Get-Content config.json` 的 `config.json`）也当 cmdlet 校验，
  导致任何带参数的命令都被误拦——白名单"更严但不可用"。改为只校验每段首个 token，跳过段内参数

### 文档
- `ISynthesisStrategy.h`：`LlmSynthesis` 注释 800→3000（实际默认值已改但注释未同步）
- `DESIGN_REVIEW.md`：更新 A6/C1+C2 状态与复核结论

### 测试
- 新增 `tests/test_shell_tools.cpp`（5 用例，黑盒验证白名单放行/拦截/注入字符/管道段/空命令）
- `test_chapter_tools.cpp`：加 `update_chapter_scenes` 悬空引用软校验用例
- `test_character_tools.cpp`：加 `update_character_relationships` 写入 + 悬空 target 软校验用例
- `test_context_manager.cpp`：加 A1 compact 真删消息用例（30→20 条）+ 消息不足跳过用例
- 全量 17/17 通过

## [2026-06-28] 设计评审批次⑤（最终轮）：语义检索激活 + 顺手修复（A2/A3/A9/A11/A13/A14）

> 最后一轮。A2+A3 是整个评审最大的"承诺 vs 现实"落差——向量检索子系统代码全写好了但从未接入。
> 顺带修 A9 设定消息自动 pin、A13 word_count 假数据、A14 CharacterDevelopment 通道、A11 NovelChunker 中文适配。

### A2+A3 — 语义检索管线激活
- `ReplHandler.cpp`：`/index` 命令从桩变成真实现（遍历章节/角色/设定/规则→NovelChunker切分→EmbeddingGenerator生成嵌入→VectorStore.insert→saveToFile持久化）
- `NovelAgentApp.h`：暴露 `vectorStore()` 和 `embeddingGenerator()` accessor
- `ReplHandler.h`：加 `setApp(NovelAgentApp*)` 后向引用
- `VectorStore.h`：`saveToFile()` 从 private 改为 public
- `ContextManager.cpp`：assemble() 语义召回加 chapter_id 去重 + 分层标签 "[补充记忆]"
- `ContextManagerTypes.h`：`ContextAssembly` 加 `has_semantic_context`

### A11 — NovelChunker 修复
- `NovelChunker.cpp`：4个工厂方法 metadata 加 `{"text", text}` 字段（修复两个消费者找不到 text 导致整条召回链路白费的 bug）
- `NovelChunker.cpp`：`overlapFromPrevious` 加 UTF-8 安全截断回退（续字节 0x80-0xBF 检测）
- `EmbeddingGenerator.cpp`：`preprocessText` 加 UTF-8 安全截断回退

### A9 — 设定消息自动 pin
- `ToolPipeline.cpp`：`executeAndAppend` 加设定工具白名单，执行后自动 `pinMessage`

### A13 — word_count 自动维护
- `ChapterTools.cpp`：`WriteChapterTool` 和 `AppendChapterTool` 写完正文后用 TokenCounter 更新 `Chapter::word_count` 和 `Project::current_word_count`

### A14 — CharacterDevelopment 通道
- `CharacterTools.h/.cpp`：新增 `AddCharacterDevelopmentTool`（参数 character_id/chapter_id/summary/category/affected_fields，ID=dev-char_id-N）

### 测试
- 全量 16/16 通过

## [2026-06-28] 设计评审批次④：状态机实现 + Shell 白名单 + 并行编排修复（D1/C1C2/A18）

> 将三项"装饰/过度/误判"补建成真正可用。全量 16/16 通过。

### C1+C2 — Shell 黑名单→白名单
- `ShellTools.cpp`：52 条黑名单 → 16 安全 cmdlet 白名单 + token 级管道解析；拦截 ` $() `` ` `. ` 注入
- `ShellTools.h`：description 更新为"只读 PowerShell 查询命令"

### D1 — 状态机可用
- `ToolCallLoop`：工具执行前后 `transition(AwaitingTool/Thinking)`
- `AgentState.cpp`：`AwaitingTool→Idle` 合法化
- `ChapterTools.cpp`：`WriteChapterTool` 覆写前检查（`allow_auto_overwrite=false` 时返回 confirm_overwrite）
- `Project.h`：加 `allow_auto_overwrite` 字段

### A18 — 并行编排修复
- `KeywordParallelDetector`：负向规则 + 双关键词联合命中
- `SubAgentTemplate`：`suggested_max_rounds` 差分配（3-8）
- `ParallelProcessor`：补 `ContextManager::assemble()` 注入
- `LlmSynthesis`：`max_result_chars` 800→3000

## [2026-06-28] 设计评审批次②：修复内存安全与死锁（B3/B5/B8）

> 依据 `docs/review/DESIGN_REVIEW.md` 评审报告，修复第二批「内存安全/死锁」类高严重度问题。

### B3 — SubAgent 超时 use-after-free
- `src/agent/SubAgent.cpp`：`execute()` 超时后改为 `future.wait()` 无条件等待异步任务彻底退出，替代"放弃等待"（此前在清理宽限期过后直接返回销毁 this，异步线程访问已析构对象导致悬空）。
- 注释自承认的逻辑已消除。最坏情况等待 HTTP read_timeout（180s），远好于 use-after-free。
- 新增 `test_sub_agent` 1 项 B3 验证（SlowMockLLMClient 睡眠 8s + 1s 超时 → 不崩溃返回 timed_out）。

### B5 — 主循环超时保护
- `src/agent/IMessageProcessor.cpp`：`SerialProcessor::process()` 中为 `ToolCallLoopConfig` 设置 `timeout=300s`（5 分钟），防止 HTTP 半开/服务端卡死时主线程永久阻塞。
- 此前 SerialProcessor 不设 timeout（默认 0→同步模式无超时），SubAgent 有 120s 超时主循环反而没有——保护不一致已修复。

### B8 — 异常后状态恢复（不卡 Thinking 永久拒输入）
- `src/agent/Agent.cpp`：`processUserMessage()` 对步骤 6-7.5（处理器调用 + 状态恢复 + 轨迹记录 + 增量保存）加 try-catch 包裹。捕获异常后强制 `transition(Error) → recover() → Idle`，返回空响应。
- 此前异常穿透到 ReplHandler 的 catch，`state_` 卡在 Thinking 导致 `canAcceptInput()` 永久返回 false 直至重启——死锁路径已消除。
- 新增 `test_agent` 1 项 B8 验证（mock 返回非法 JSON → 异常后 canAcceptInput() 为 true，状态为 Idle）。

### 顺带修复 — ContextManager token 阈值测试 3 项既有失败
- `src/agent/ContextManager.cpp`：`recordUsage()` 同步更新 `current_context_size_ = input_tokens`（API 返回的 prompt_tokens 比启发式估算更精确）。
- 修复后 `test_context_manager` 中 `usagePercent`/`checkThresholds`/`has_critical` 三项由失败变通过。

### 测试
- 全量 **16/16 全部通过**（test_context_manager 遗留失败已消除）。
- 新增：`test_sub_agent` B3 超时测试、`test_agent` B8 异常恢复测试。

## [2026-06-28] 设计评审批次①：堵住数据丢失与静默失效（B1/A5/D2/B2）

> 依据 `docs/review/DESIGN_REVIEW.md` 评审报告，修复第一批「会丢用户数据 / 静默失效」的高严重度问题。
> 范围：可靠性底线性修复，零风险高收益，不涉及架构改动。

### B1 — writeText 原子化（temp + rename）
- `src/utils/FileUtils.cpp`：`writeText` 改为先写同目录临时文件 `<path>.tmp.<seq>`，flush 落盘后 `fs::rename` 原子替换目标，rename 失败回退「删目标再 rename」。
- 覆盖所有持久化路径（ProjectIO 6 个 JSON / SessionPersistence / VectorStore / writeChapter / ExecutionTracer），崩溃写到一半不再产生半截损坏文件（原 B6 vectors.json 一并受益）。
- 新增 `tests/test_file_utils.cpp`（6 项：往返/覆盖/无临时残留/父目录自动创建/大内容完整/缺失文件返回空）。

### A5 — project.json 错路径改为 novel.json
- `src/agent/ContextManager.cpp`：3 处 `last_write_time(".../project.json")` 写死路径改为引用导出常量，抽取 `projectSettingsMtime()` helper。
- `src/project/ProjectIO.h`：导出文件名常量 `kNovelJsonFileName` 等（单一来源），`ProjectIO.cpp` 内部短名常量改为引用导出常量，消除字面量漂移根源。
- 修复后 `isVectorStoreStale()` 与「Project 修改后清空旧摘要」的 mtime 一致性保障恢复正常（此前因文件名不匹配整条静默失效）。
- 顺手修正过时注释：`SessionPersistence.h`、`Project.h` 的 project.json → novel.json。
- 新增 `test_context_manager` 2 项 A5 验证（isVectorStoreStale 读 novel.json mtime / saveSessionState 记录非零 mtime）。

### D2 — config context_window 字段迁移兼容
- `src/config/AppConfig.h`：`ProviderConfig` 弃用 `NLOHMANN_DEFINE_TYPE_INTRUSIVE`，手写 `to_json`/`from_json`。
- `from_json` 优先读新字段 `max_context_tokens`，缺失时回退读旧字段 `context_window`（commit 51b7616 重命名后未迁移旧 config.json，致用户配置静默失效）。
- `to_json` 只写新字段名，保存时自动升级格式。
- 新增 `tests/test_app_config.cpp`（6 项：旧字段读取/新字段读取/新旧优先/默认值/升级保存/旧 config 往返升级）。

### B2 — 会话增量保存
- `src/agent/Agent.cpp`：`processUserMessage` 末尾（maybeAutoCompact 后）调用 `saveSessionState()`，每轮对话落盘到 conversation.json + session_meta.json，写入失败 try/catch 不阻断主流程。
- 此前仅在 REPL 退出时保存一次，长会话写作中途崩溃会丢失本轮全部对话与创作上下文。
- 删除从未被调用的死代码 `NovelAgentApp::saveConversationIfNeeded`（.h 声明 + .cpp 空壳定义）。
- 新增 `test_agent` 1 项 B2 验证（processUserMessage 后 conversation.json 含 user+assistant 两条）。

### 测试
- 新增测试套件：`test_file_utils`、`test_app_config`（均注册进 tests/CMakeLists.txt）。
- 全量 16 个套件 15 通过；test_context_manager 仍有 3 项开工前既有的 token 阈值计算失败（usagePercent/checkThresholds/has_critical），与本批次无关。

## [2026-06-28] 修复 Model 字段 LLM 写入能力缺口：新增 update_chapter / create_setting / create_world_rule / update_style + 扩展 create_chapter / create_character

### 新增工具
- **`update_chapter`** — 更新章节创作简报字段（15 string + 7 array 白名单，仿 update_character 模式）
- **`create_setting`** — 创建世界观设定（setting-001 格式 ID，6 个可选叙事字段）
- **`create_world_rule`** — 创建世界规则（rule-001 格式 ID，5 个可选字段）
- **`update_style`** — 更新写作风格配置（19 string + 1 int + 3 array 白名单，Style 单例无需 id）

### 工具扩展
- **`create_chapter`** — 参数从 2→16，创建时可填充 goal/conflict/hook 等叙事简报字段
- **`create_character`** — 参数从 2→12，创建时可填充 personality/background/goal 等
- **`update_setting`** — 新增 related_characters/related_plot_threads/related_rule_ids/tags 数组字段
- **`update_world_rule`** — 新增 related_settings/tags 数组字段

### Bug 修复（代码审查）
- 修复 `UpdateChapterTool` 的 `const_cast` 脆性：`findChapter` 改为返回非 const 指针
- 修复 `UpdateStyleTool` int 字段处理器：`kIntFields` set 改为 `kIntMap` ptr-to-member map，消除潜在幽灵 bug
- 修复 `UpdateCharacterTool` 白名单缺失：`tags` 加入 `kArrayMap`
- 修复 `CreateCharacterTool` 头文件注释：更新为反映完整 12 参数

## [2026-06-28] 新增 LLM 主动查询工具：search_memory / read_style + 扩展 get_project_status

### 新增工具

- **`search_memory`** — 显式语义搜索工具，允许 LLM 用自定义 query 主动查询向量存储
  - 文件：`src/agent/tools/SearchMemoryTools.h/.cpp`
  - 注册方式：手动工厂（仿 ShellTools，不接收 Project&）
  - 后端注入：`initSearchMemoryBackend()` 在 `NovelAgentApp::setupAgent()` 中调用
  - 参数：`{query: string, top_k?: integer(默认5)}`
  - 分类：`ToolCategory::System`
- **`read_style`** — 写作风格查询工具，返回完整 Style 配置（24 字段）
  - 文件：`src/agent/tools/StyleTools.h/.cpp`
  - 注册方式：标准 `REGISTER_TOOL` 宏
  - 分类：`ToolCategory::Setting`

### 工具扩展

- **`get_project_status`** — 从 8 字段扩展到 23 字段
  - 新增：`description`, `genre`, `comps`, `central_question`, `ending_type`, `target_word_count`, `current_word_count`, `status`, `must_have_elements`, `must_avoid_elements`, `narrative_promises`, `tags`, `world_rules_summary`, `created`, `modified`

### 构建

- `cmake/Sources.cmake` — `NOVELAGENT_TOOLS` 追加 SearchMemoryTools 和 StyleTools 文件对
- `src/NovelAgentApp.cpp` — `setupAgent()` 中在 `registerAllTools()` 前调用 `initSearchMemoryBackend()`

## [2026-06-25] 3 项代码审查修复 + 上下文模块注释补充

### Bug 修复（f64c304 提交后审查）

- **修复** — `vector_store_dirty_` 未持久化到 `SessionMeta`：`/rewind` 后重启应用导致向量脏标记丢失
  - `SessionMeta` 新增 `vector_store_dirty` 字段
  - `saveMeta()` / `loadMeta()` 对称序列化/反序列化
  - `saveSessionState()` / `loadSessionState()` 写入/恢复该字段
- **修复** — `Agent::execute()` 未做输入校验：`execute()` 补充 `validateInput()` 调用，与 `processUserMessage()` 保持一致
- **修复** — `compact()` 中 `ctx.substr(0, 500)` 可能在 UTF-8 多字节字符中间截断：添加 UTF-8 续字节检测循环

### 注释补充

- **ContextManager.h** — `assemble()` 文档扩展为完整 7 步流水线
- **ContextManager.cpp** — 补充 `buildSystemPrompt`、`compact`、`isVectorStoreStale`、`setRetrievalBackend`、`vector_store_dirty_` 的详细中文注释
- **SessionPersistence.h** — `SessionMeta` 所有字段添加 Doxygen 注释；`SessionPersistence` 类补充双文件设计说明
- **SessionPersistence.cpp** — `save()` / `load()` 补充序列化格式和防御式解析说明
- **ToolCallLoop.h** — `run()` 的 `initial_messages` 参数补充完整文档（为什么存在 + 首轮/后续轮次行为差异）
- **Agent.h** — `compactConversation` / `rewindTo` / `saveSessionState` / `loadSessionState` / `maybeAutoCompact` 扩展 docstring
- **Agent.cpp** — `validateInput`（两层防御说明）、`resetSession`（级联注释）、`saveSessionState`/`loadSessionState`（流程注释）
- **IMessageProcessor.cpp** — 同步 compact 步骤补充逃生阀设计原理注释

## [2026-06-20] LLMClientFactory — 实例级线程隔离 + 5 个预存在 Bug 修复

### Phase 4 线程安全：实例级隔离

- **新增** — `src/llm/LLMClientFactory.h/.cpp`：工厂类，封装 `ProviderConfig`，`create()` 返回独立 `LLMClient` 实例
  - 工厂本身不可变（线程安全），可在多线程间共享
  - 每个 `Agent` / `SubAgent` / `AgentOrchestrator` 通过工厂创建自己的 `LLMClient`
  - `AgentOrchestrator` 为每个并行 `SubAgent` 创建独立客户端
  - `SessionManager` 为每个 Session 创建独立 `Agent`（从而独立 `LLMClient`）
- **修改** — `Agent` / `SubAgent` / `AgentOrchestrator` / `SessionManager` / `BackendServer` 全部改用工厂模式
  - `Agent` 持有 `unique_ptr<ILLMClient>` + `LLMClientFactory&`（用于 `useParallelProcessor`）
  - `SubAgent` 新增测试用构造函数 `SubAgent(unique_ptr<ILLMClient>, IToolProvider&)`
  - `ParallelProcessor` → `AgentOrchestrator` 链路全部通过工厂创建独立客户端
- **注释** — 所有相关类的线程安全注释更新（`LLMClient` / `HttpClient` / `Agent` / `SubAgent` / `AgentOrchestrator` / `SessionManager` / `BackendServer`）

### Bug 修复（预存在，本次审查发现并修复）

- **修复** — `AgentOrchestrator::executeParallel()` 节流循环双重消费 `std::future` 导致 `std::future_error` 崩溃
  - 引入 `consumed[]` 追踪已在节流中收集的 future，最终收集循环跳过已消费项
  - 同时添加 `std::this_thread::yield()` 消除节流轮询忙等待
- **修复** — `BackendServer::/api/chat` 同一会话并发请求导致 Agent 数据竞争
  - `Session` 新增 `std::mutex request_mutex`，请求线程调用 `processUserMessage()` 前加锁串行化
- **修复** — `SubAgent::execute()` 超时后 `future.wait()` 无超时限制，HTTP 挂起时无限阻塞
  - 改为 `future.wait_for(config.timeout * 2)`，超时后记录错误并返回（避免调用方永久卡死）
- **修复** — `ParallelProcessor::process()` 静默丢弃流式回调和 `raw_response`
  - 填充 `raw_response.content` / `finish_reason`
  - 调用 `callbacks.on_complete` / `on_error`
  - 添加异常 try-catch

## [2026-06-11] Tauri 桌面 GUI v0.1.0

- **新增** — `gui/` 目录：Tauri v2 + React 19 + TypeScript 桌面应用
  - React 前端（29 个源文件）：ChatPanel / MessageBubble / StreamingText / ChatInputBar / Sidebar / TopBar / AppLayout
  - Tauri Rust 后端（`src-tauri/`）：Sidecar 生命周期管理（启动/健康检查/关闭）、项目路径记忆、文件夹选择对话框
  - Catppuccin Mocha 深色主题，Markdown 流式渲染，可折叠思考链，自动滚底
  - 技术栈: Vite 6 + Tailwind CSS v4 + Zustand + react-markdown + rfd
- **新增** — 后端 API 补充（`src/server/BackendServer.cpp`）：
  - 全局 CORS 中间件（`set_pre_routing_handler`）— 所有路由自动添加跨域头 + OPTIONS 预检
  - `GET /api/project/chapters` — 章节列表（id/title/order/synopsis/status/scenes_count）
  - `GET /api/project/characters` — 角色列表（id/name/role/traits/appearances_count）
- **打包** — NSIS 安装包 7.8MB（含 C++ 后端 22MB + 9 个 MinGW DLL + React 前端 404KB）
- **路径** — 首次启动弹出文件夹选择框，之后自动记住（`%APPDATA%/novelagent/last_project.txt`）
- **环境** — Rust 1.96.0 安装至 `D:\Rust\`，USTC 镜像加速
- **构建** — `npm run tauri:build` 一键产出安装包；`npm run copy:sidecar` 复制 C++ 二进制 + DLL

## [2026-06-10] FTXUI TUI — 类 Claude Code 终端界面

- **新增** — `src/tui/` 模块（5 个组件，纯 C++20 + FTXUI）:
  - `TuiApp` — 主控制器：ScreenInteractive 事件循环、组件树组装、Worker 线程调度
  - `TuiChatPanel` — 聊天面板：流式消息渲染、多角色颜色区分（用户/助手/错误/系统）
  - `TuiInputBar` — 输入栏：命令历史（↑↓）、Enter 提交、占位提示
  - `TuiStatusBar` — 状态栏：模式标签（就绪/思考中/执行工具/错误）、Token 用量、项目信息
  - `TuiSidebar` — 侧边栏：大纲列表 + 角色列表（通过 Project 数据）
- **线程模型** — Worker 线程调用 Agent + `screen.Post()` 桥接 `StreamCallbacks`，UI 不冻结
- **斜杠命令** — `/help` `/exit` `/status` `/clear`（扩展自 CLI CommandParser）
- **CLI 入口** — `novelagent --tui -p ./项目` 启动 FTXUI 终端界面
- **依赖** — FTXUI 6.1.9（MSYS2 `mingw-w64-x86_64-ftxui`），动态链接 3 个 DLL（~2MB）
- **修改** — `NovelAgentApp` 新增 `runTui()`，`main.cpp` 新增 `--tui` 标志
- **修改** — `CMakeLists.txt` 新增 `novelagent_tui` object library
- **新增** — `tests/test_tui.cpp`：12 个 TUI 组件测试（ChatPanel/InputBar/StatusBar/Sidebar）
- 三种模式共存: `novelagent`（REPL）/ `--tui`（FTXUI）/ `backend`（HTTP+SSE）
- 测试统计: **15/15** 全部通过（新增 1 个测试目标）

## [2026-06-10] 清理 — 移除前端代码，回归纯 C++ 后端

- **移除** — Node.js Ink/React TUI 前端（`tui/` 目录，7 个 TypeScript 文件）
- **移除** — TUI-Web 页面（`tui-web/index.html`）
- **移除** — 启动脚本（`start.bat`、`start.sh`）
- **移除** — `main.cpp` 中的 `launchDesktop()` 函数 + `--tui` CLI 选项
- **移除** — `.gitignore` 中 `tui/my_novel/` 条目
- **保留** — C++ 后端 Server（`BackendServer`、`SessionManager`），纯 HTTP+SSE API，前端由外部实现
- 项目回归为纯 C++20 代码库

## [2026-06-10] Phase 6 — 前后端分离 + Node.js Ink TUI

- **C++ 后端 Server** — `src/server/BackendServer.h/.cpp` + `SessionManager.h/.cpp`:
  - HTTP+SSE 服务器（基于 httplib），支持多终端同时连接
  - SessionManager：多会话管理（创建/销毁/空闲清理），线程安全
  - SSE 流式聊天：`set_chunked_content_provider` 实现真正的流式响应
  - API 路由：`/api/chat`(SSE) `/api/session` `/api/execute` `/api/project/status` `/api/project/export` `/api/health`
  - 端口文件机制（`.novelagent/port`）供前端自动发现后端
- **新增** — 4 个 C++ 文件
- 测试统计: **14/14** 全部通过

## [2026-06-10] Phase 5 — 打磨 + 终端 GUI (7步全部完成)

- **5.1** — `AnsiTerminal.h`: 统一 ANSI 工具库（颜色/样式/光标/语义主题）
  - Windows `SetConsoleMode` 自动启用 ANSI 支持
  - 语义化颜色：assistant(绿)/userInput(蓝)/toolCall(灰)/thinking(暗)/error(红)/warning(黄)
- **5.2** — Tab 补全 + 4 个新斜杠命令:
  - `/status` — 项目统计（章节/角色/设定/字数/对话）
  - `/config <key> <value>` — 运行时配置（context_window, max_tool_rounds）
  - `/export` — 导出所有章节为单个 Markdown 文件
  - `/save` — 手动保存项目
  - `/trace` — 执行轨迹查询
  - Tab 补全：输入 `/` 后自动补全命令名
- **5.3** — 错误恢复:
  - main.cpp 最外层 try/catch（全局异常兜底）
  - ReplHandler 自动保存（崩溃前保存项目）
  - 磁盘写入失败的友好提示
- **5.4** — `AgentState.h`: 显式状态机
  - `AgentState` 枚举（Idle/Thinking/AwaitingTool/WaitingUser/Error/Fatal）
  - `StateMachine` 类：状态转换 + 合法性检查 + 日志
  - 状态名中文化（"就绪"/"思考中"/"执行工具"等）
- **5.5** — `ParameterValidator.h/.cpp`: 工具参数 Schema 校验
  - 必填字段检查、类型匹配（string/integer/boolean/array/enum）
  - additionalProperties 检测（记录 warning 不阻断）
  - 校验失败返回结构化 JSON 错误 `{"error":"...","details":[...]}`
  - 集成到 `ToolPipeline::executeOne()` 执行前
- **5.6** — `ExecutionTracer.h/.cpp`: Agent 执行轨迹记录
  - `TraceEntry` 结构（timestamp/step_index/event_type/payload/tokens/duration）
  - `dump()` 保存为 JSONL 格式到 `.novelagent/traces/`
  - `summary()` 汇总统计（总步数/token/LLM调用/工具调用/错误）
  - `recentSummary(n)` 最近 N 步文本摘要
- **终端 GUI** — `TerminalGUI.h/.cpp`: Claude Code CLI 风格界面
  - 语义化颜色主题（角色区分）
  - 状态栏渲染（模式 | 项目 | token 用量）
  - Markdown 渲染（**粗体**/*斜体* → ANSI 转义码）
  - 进度指示器（旋转动画）
  - 命令历史管理
  - 标题/分隔线渲染
- **修改** — StreamDisplay(重写-ANSI主题), ReplHandler(重写-GUI+命令), ToolPipeline(校验), NovelAgentApp(项目传递), main(ANSI+错误恢复)
- **新增** — 11 个文件: AnsiTerminal, TerminalGUI, AgentState, ParameterValidator, ExecutionTracer
- 测试统计: **14/14** 全部通过
- **版本**: v0.3.0

## [2026-06-10] 架构深层重构 — 依赖倒置+策略模式+安全约束 (P0-P3)

- **P0** — `ToolCallLoop`: 提取 Agent/SubAgent 中 ~90 行重复 tool call 循环为独立引擎
  - 支持超时控制、首轮流式/非流式配置、统一错误处理
  - Agent::runToolLoop 和 SubAgent::execute 均委托 ToolCallLoop
- **P0** — `IToolProvider` + `RestrictedToolProvider`: 工具访问安全约束
  - SubAgent 不再持有完整 ToolRegistry&，改为持有 IToolProvider&
  - RestrictedToolProvider 白名单机制在类型系统层面保证安全
  - O(n*m) 过滤优化为 O(n)
- **P1** — `ISynthesisStrategy`: AgentOrchestrator 汇总策略接口
  - LlmSynthesis（LLM汇总）、ConcatSynthesis（简单拼接）、CustomSynthesis（注入函数）
  - AgentOrchestrator::synthesize() 不再硬编码 LLM 调用
- **P1** — `IMessageProcessor`: 消除 Agent 硬编码串行/并行分支
  - SerialProcessor（tool call 循环）、ParallelProcessor（委托 Orchestrator）
  - 新增 PlanThenExecute 模式只需实现接口并注入
- **P1** — `AgentOrchestratorTypes.h`: 分离 SubTask 类型到独立头文件
- **新增** — 10 个文件: ToolCallLoop, IToolProvider, IMessageProcessor, ISynthesisStrategy, AgentOrchestratorTypes
- **修改** — Agent(重写-策略模式), SubAgent(IToolProvider), AgentOrchestrator(ISynthesisStrategy), NovelAgentApp(适配)
- 测试统计: **14/14** 全部通过

## [2026-06-10] Phase 4 架构重构 — 审查问题修复 (P0-P3)

- **P0** — `HttpClient`: 提取共享 HTTP 基础设施（URL解析/认证/重试/错误处理）
  - LLMClient 和 EmbeddingGenerator 均通过组合持有 HttpClient，消除 ~150 行重复代码
  - LLMClient.cpp: 从 363 行精简到 140 行（-61%）
  - EmbeddingGenerator.cpp: 从 262 行精简到 155 行（-41%）
- **P1** — 检索模块抽象接口: `IVectorStore` + `IEmbeddingGenerator`
  - VectorStore 和 EmbeddingGenerator 改为实现纯虚接口
  - 支持 Mock 测试，未来可替换为 sqlite-vec / ONNX 后端
- **P1** — 拆分 ContextManager 上帝类（7→1 职责）:
  - `ConversationSummarizer` — 对话摘要（规则提取+渲染）
  - `ChapterSummaryCache` — 章节摘要缓存 CRUD（通过 IStorageBackend）
  - `DegradationPipeline` — 策略模式降级管线（5个独立策略类）
  - `SessionPersistence` — 会话保存/加载/归档
  - ContextManager 精简为编排器（~150 行），组合 4 个子模块
- **P2** — ContextManager 通过 IStorageBackend 访问存储:
  - `FileStorageBackend` 适配 ProjectIO → IStorageBackend
  - ContextManager 构造函数注入 `IStorageBackend&`，不再直接依赖 ProjectIO
  - 符合 CLAUDE.md 架构规则
- **P2** — 降级策略模式: `IDegradationStrategy` + 5 个具体策略类 + `DegradationPipeline`
  - 新增降级等级只需实现接口并注册，符合开闭原则
- **P3** — `SummaryKeywords` 配置化: 剧情/任务关键词可通过构造函数或 setter 定制
- **新增** — 15 个文件: Http客户端、4个抽象接口、4个拆分类、FileStorageBackend、ContextManagerTypes
- **修改** — ContextManager(重写)、LLMClient(瘦身)、EmbeddingGenerator(瘦身)、NovelAgentApp(适配)
- 测试统计: **14/14** 全部通过

## [2026-06-09] Phase 4 — 上下文管理与语义检索 (9步)

- **新增** — `ContextManager` Phase 4 完整版:
  - 对话历史摘要 `summarizeConversation()`：规则提取角色名/章节引用/剧情点/任务
  - 章节摘要缓存：`.novelagent/summaries.json` 读写，支持按章节 ID 索引
  - 预算分配 `allocateBudget()`：50/30/20 规则（章节/对话/摘要）
  - 多级降级（L1-L5）：截断章节→移除角色详情→移除相邻章节→截断对话→全文压缩
  - 会话持久化 `saveSession()`/`loadSession()`/`archiveSession()`
- **新增** — `VectorStore` (JSON 后端 + 暴力余弦相似度):
  - CRUD: insert/insertBatch/remove/update + 持久化到 JSON 文件
  - 搜索: `search()` Top-K 余弦相似度排序，< 10ms @ 万级向量
  - API 兼容 sqlite-vec（后续仅需替换 .cpp 内部实现）
- **新增** — `EmbeddingGenerator`:
  - 调用 OpenAI 兼容 `/v1/embeddings` API
  - 支持单条/批量嵌入，自动分批（max_batch_size=100）
  - 指数退避重试（3 次）+ 文本截断预处理
- **新增** — `NovelChunker`:
  - 章节切分：优先按 Scene 边界，退化为段落边界（500-2000字/chunk, 15% 重叠）
  - 实体拼接：`chunkCharacter()`/`chunkSetting()`/`chunkWorldRule()` 生成可嵌入文本
- **新增** — `tests/test_retrieval.cpp`：15 个检索模块测试
- **修改** — `tests/test_context_manager.cpp`：扩展至 22 个测试（Phase 4.1-4.4）
- **修改** — `CMakeLists.txt`：新增 `src/retrieval/` 模块到 novelagent_core
- 测试统计: **14/14** (新增 1 个测试目标，test_context_manager 扩增 16 子测试)

## [2026-06-09] Phase 3.5 — 多Agent并行编排 (9步)

- **新增** — `SubAgent`: 独立对话上下文 + 受限工具集(std::async) + 120s超时
- **新增** — `AgentOrchestrator`: 分解→并行→汇总 (max_parallel=4, std::async)
- **新增** — `SubAgentTemplate`: 5个内置模板(chapter-consistency等)
- **新增** — `TemplateManager`: 内置+用户模板CRUD
- **修改** — `ReplHandler`: /parallel + /agent 命令族
- **修改** — `NovelAgentApp`: 集成AgentOrchestrator+TemplateManager
- 测试统计: 12/12

## [2026-06-09] Phase 3.6-3.12 — 剩余工具 + REPL 集成（Phase 3 核心完成）

- **新增** — Setting 工具: `get_setting` / `get_settings` / `update_setting`
- **新增** — WorldRule 工具: `get_world_rule` / `get_world_rules` / `update_world_rule`
- **新增** — Outline 工具: `get_outline`
- **新增** — Project 工具: `get_project_status`
- **新增** — Shell 工具: `run_powershell`（`_popen` 捕获 stdout + exit_code）
- **新增** — `AgentSetup.h`: `registerAllTools()` 一键注册全部 17 个工具
- **新增** — `CommandParser`: 斜杠命令解析（/help /exit /clear /tools /model）
- **新增** — `StreamDisplay`: 流式输出包装（内容/思维链/工具调用/token 统计）
- **新增** — `ReplHandler`（完整版）: REPL 主循环 + 流式显示 + 命令拦截
- **修改** — `main.cpp`: 完整集成 CLI（-p 项目 -e 单次 --provider -v）
- 工具总数: **17 个** (Chapter 5 + Character 4 + Setting 3 + WorldRule 3 + Outline 1 + Project 1 + Shell 1)
- 测试统计: 12/12 全部通过
- CLI 验证: `novelagent -p test -e "..."` --exec 模式端到端跑通

## [2026-06-09] Phase 3.5 — Character 工具（4 个）

- **新增** — `src/agent/tools/CharacterTools.h/.cpp`：4 个角色管理工具类
  - `GetCharacterTool` — 按 ID 查询角色完整档案（利用 Models.h 的 to_json 序列化）
  - `ListCharactersTool` — 列出所有角色摘要（id/name/role/goal）
  - `CreateCharacterTool` — 创建角色（自动生成 char-001 格式 ID，重名检测，补零对齐）
  - `UpdateCharacterTool` — 更新角色字段（指针到成员 map 驱动，支持 16 个 string + 4 个 array 字段）
- **新增** — `tests/test_character_tools.cpp`：6 个测试（创建/查询/列表/更新/错误处理/ToolRegistry）
- 测试统计：12/12 全部通过（新增 1 个测试目标）

## [2026-06-09] 第三轮代码审查修复 — REVIEW_NOTES.md 11 问题

- **修复** — #1 Agent 不再覆盖用户的 `system_prompt_`：ContextManager 产出用局部变量拼接（审查发现的回归 bug）
- **修复** — #2 `truncateMessages()` token 公式统一为 `countMessages()` 循环重算（审查发现的 bug）
- **修复** — #3 `buildSystemPrompt()` 无效章节 fallback 到项目概述（审查发现的 bug）
- **修复** — #4 `truncateMessages()` budget ≤ 0 时返回空列表（审查发现的 edge case）
- **修复** — #6 `CreateChapterTool` 恢复全量保存（审查发现的回归 bug）
- **修复** — #7 工具执行结果 4000 字符截断（防止单条消息超出 token 预算）
- **修复** — #8 删除 `processUserMessage` 中空 `try/catch`（审查发现的死代码）
- **修复** — #9 `ListChaptersTool` 描述与实际行为同步（审查发现的文档不一致）
- **修复** — #11 `ContextAssembly::total_tokens` 注释标注为估算值
- 暂缓 — #5 Project& 生命周期约束（等 Phase 3.5）+ #10 additionalProperties 安全默认
- 影响范围：`Agent.cpp`、`ContextManager.h/.cpp`、`ChapterTools.h/.cpp`
- 测试统计：11/11 全部通过

## [2026-06-09] Step 3.1-3.4 代码审查修复

- **修复** — Agent 集成 ContextManager：`runToolLoop` 每次 LLM 调用前做 token 预算截断（可选，通过 `setContextManager()` 启用）
- **修复** — `ListChaptersTool` 不再逐章读取文件（100 章 = 省 100 次磁盘 I/O），改为只返回元数据
- **修复** — 删除 `countWords()` 重复实现（与 `TokenCounter` 功能重复且算法不一致）
- **修复** — `CreateChapterTool` 只保存 `outline.json` 而非全部 6 个 JSON 文件
- **修复** — Agent 新增 `setContextWindow()` 配置入口
- 测试统计：11/11 全部通过

## [2026-06-08] Phase 3.4 — Chapter 工具（5 个）

- **新增** — `src/agent/tools/ChapterTools.h/.cpp`：5 个章节操作工具类
  - `ReadChapterTool` — 读取章节 Markdown 全文
  - `WriteChapterTool` — 覆写章节内容
  - `CreateChapterTool` — 创建新章节 + 更新 outline + 写入文件
  - `AppendChapterTool` — 读取 → 追加 → 写回
  - `ListChaptersTool` — 列出所有章节 ID/标题/顺序/字数
- **新增** — 每个工具持有 `Project&` 引用，通过 `ProjectIO` 执行磁盘 I/O
- **新增** — `tests/test_chapter_tools.cpp`：7 个集成测试（临时目录 + 真实文件 I/O）
- **修复** — 文件名使用章节 ID（`ch-001.md`）而非标题，避免 Windows 窄字符 API 下 UTF-8 路径问题
- 影响范围：`src/agent/tools/ChapterTools.h/.cpp`、`tests/test_chapter_tools.cpp`
- 测试统计：11/11 全部通过（新增 1 个测试目标）

## [2026-06-08] Phase 3.3 — ContextManager（基础版）

- **新增** — `src/agent/ContextManager.h/.cpp`：上下文管理器，负责 token 预算计算 + 消息截断 + 系统提示词构建
- **新增** — `ContextAssembly` 结构体：截断后消息 + 系统提示词 + 预算统计 + 截断元信息
- **新增** — `calculateBudget()`：80/20 规则（80% 输入 + 20% 输出预留）
- **新增** — `truncateMessages()`：从旧到新移除超出预算的消息，保证最新消息不丢失
- **新增** — `buildSystemPrompt()`：委托 PromptContextBuilder 按章节构建系统提示词
- **新增** — `tests/test_context_manager.cpp`：6 个测试（预算计算、不截断、截断触发、无 Project、有/无章节）
- 影响范围：`src/agent/ContextManager.h`、`src/agent/ContextManager.cpp`、`tests/test_context_manager.cpp`
- 测试统计：10/10 全部通过（新增 1 个测试目标）

## [2026-06-08] Phase 3.2 — Agent 核心循环

- **新增** — `src/agent/Agent.h/.cpp`：核心 Agent 类，实现 `processUserMessage()` 和 `execute()` 两种入口
- **新增** — Tool call 循环：LLM 请求工具 → Agent 执行 → 回传结果 → 再次调用 LLM（最多 10 轮）
- **新增** — 首轮流式 + 后续非流式的混合调用策略（用户看到实时输出，工具循环节省开销）
- **新增** — 对话历史自动管理：用户消息、assistant 回复、tool 结果自动追加到 Conversation
- **新增** — `tests/test_agent.cpp`：5 个 Mock HTTP 测试（简单对话、tool call 循环、execute 模式、对话管理、空输入）
- 影响范围：`src/agent/Agent.h`、`src/agent/Agent.cpp`、`tests/test_agent.cpp`、`CMakeLists.txt`、`tests/CMakeLists.txt`
- 测试统计：9/9 全部通过（新增 1 个测试目标）

## [2026-06-08] Phase 3.1 — ToolRegistry + 内置工具架构

- **新增** — `src/agent/tools/BuiltInTool.h`：工具抽象基类 + `ToolCategory` 枚举（7 个类别）+ `toDefinition()` 转换
- **新增** — `src/utils/SchemaUtils.h`：JSON Schema 构建辅助（`object` / `stringProp` / `integerProp` / `booleanProp` / `stringEnum` 等）
- **新增** — `src/agent/ToolRegistry.h/.cpp`：工具注册中心，支持 `registerTool()`（函数式）和 `registerBuiltInTool()`（类式）两种注册方式
- **新增** — `tests/test_tool_registry.cpp`：8 个测试（函数式/类式注册、执行、ToolDefinition 输出、错误处理、分类查询、SchemaUtils）
- 影响范围：`src/agent/`、`src/utils/SchemaUtils.h`、`CMakeLists.txt`、`tests/CMakeLists.txt`
- 测试统计：8/8 全部通过（新增 1 个测试目标）

## [2026-06-08] 新增 LLM 请求→响应流程图文档

- **新增** — `docs/diagrams/` 目录：存放流程图和架构图
- **新增** — `docs/diagrams/LLM请求响应流程图.md`：Mermaid 格式的完整流程图（含 7 张图）
  - 总览：非流式 vs 流式两条路径对比（flowchart）
  - 详细时序图：从用户输入到 LLMResponse 返回（sequence diagram, 7 个阶段）
  - 组件数据流图：SSE 文本 → StreamChunk → LLMResponse 的类型转换链
  - 错误处理路径：4 层错误检测（配置/HTTP/SSE/完整性）及中文错误映射
  - 数据结构对照表：各阶段数据类型的来源/去向
  - 非流式 vs 流式对比表
- 可在 VS Code 中按 `Ctrl+Shift+V` 直接预览 Mermaid 渲染效果

## [2026-06-08] 编译速度优化 — 对象库消除重复编译

- **新增** — CMake 对象库 `novelagent_lib`：所有业务源码编译为 `.o` 集合，主程序和测试共享
- **修改** — 每个 `.cpp` 从编译 2~4 次降为 1 次（`SSEParser.cpp`: 4×→1×, `LLMClient.cpp`: 3×→1×, `StreamAccumulator.cpp`: 3×→1×）
- **修改** — 测试目标大幅简化：每个测试从 ~10 行（include 路径 + 链接库 + 源文件列表）简化为 ~5 行
- **修改** — 构建生成器从 MSYS Makefiles 切换为 Ninja（自动检测），增量构建更快
- **修改** — `add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT)` 从全局改为 `target_compile_definitions` 精确作用域
- **修改** — MSYS2 DLL PATH 覆盖所有测试目标（对象库 PUBLIC 链接使所有测试都依赖 OpenSSL/spdlog DLL）
- 影响范围：`CMakeLists.txt`、`tests/CMakeLists.txt`
- 测试统计：7/7 全部通过，增量构建 ~12s（修改 1 个 `.cpp`）

## [2026-06-08] Message.h 协议构造代码封装

- **新增** — `Message` 静态工厂方法：`user()`、`system()`、`assistant()`、`toolResult()`，替代冗长的聚合初始化
- **新增** — `Conversation` 类（`src/llm/Conversation.h`）：封装对话历史管理，提供 `addUser()`、`addAssistant()`、`systemPrompt()`、`messages()` 等便捷方法
- **新增** — `tests/test_sse_helpers.h`：SSE 测试数据构造辅助（`sseContentChunk` / `sseFinishChunk` / `sseToolCallChunk`），消除测试中手工拼接 JSON 字符串
- **修改** — `test_llm_client.cpp`：迁移至新 API（工厂方法 + SSE 辅助），删除手工 JSON 拼接代码
- **修改** — `test_deepseek_smoke.cpp`：迁移至 `Message::user()` 工厂方法
- 影响范围：`src/llm/Message.h`、`src/llm/Conversation.h`、`tests/test_sse_helpers.h`、`tests/test_llm_client.cpp`、`tests/test_deepseek_smoke.cpp`
- 测试统计：7/7 全部通过

## [2026-06-08] 流式响应字段封装 — StreamingTypes 分离 + StreamingPipeline

- **新增** — `src/llm/StreamingTypes.h`：从 Message.h 拆分出 `ToolCallDelta`、`UsageInfo`、`StreamChunk` 三个流式中间类型
- **新增** — `src/llm/StreamingPipeline.h`：封装 SSEParser + StreamAccumulator + 回调路由为统一流式管道 facade
- **修改** — `LLMClient::chat()`：管道装配从 ~40 行简化为 ~15 行，使用 `StreamingPipeline`
- **修改** — `SSEParser.h` / `StreamAccumulator.h`：include 改为直接引用 `StreamingTypes.h`
- **修改** — `Message.h`：移除流式类型（~35 行），末尾 `#include "StreamingTypes.h"` 保持向后兼容
- 影响范围：`src/llm/Message.h`、`src/llm/StreamingTypes.h`、`src/llm/StreamingPipeline.h`、`src/llm/SSEParser.h`、`src/llm/StreamAccumulator.h`、`src/llm/LLMClient.cpp`
- 测试统计：7/7 全部通过

## [2026-05-31] 依赖管理迁移 MSYS2 + Phase 2 代码审查修复

- **新增** — MSYS2 pacman 优先 + FetchContent 回退的二级依赖管理（`find_package` → `FetchContent`）
- **新增** — `docs/review/DEFERRED.md` 暂缓问题记录（PCH、Volume/Chapter 字段重叠）
- **修改** — 依赖：nlohmann_json v3.12.0、CLI11 v2.6.2、spdlog v1.17.0（MSYS2 预编译包）
- **修改** — cpp-httplib 启用 OpenSSL 支持（`HTTPLIB_USE_OPENSSL ON`）
- **修改** — `AppConfig::load()` 加载顺序：当前目录 config.json 优先于全局 `~/.novelagent/`
- **修复** — `LLMClient.h` 删除未使用的重复超时常量（`kConnectionTimeout`/`kReadTimeout`）
- **修复** — `TokenCounter::estimateEnglishWords` 修复 `std::isalpha` 对非 ASCII 字符的 UB
- **修复** — `PromptContextBuilder::selectPlotThreads` POV 为空时的回退逻辑改进
- **修复** — 测试运行时 DLL 找不到：MSYS2 动态库路径加入 `ENVIRONMENT_MODIFICATION`
- **修改** — CMake 最低版本升至 3.24
- **修改** — `CPPHTTPLIB_OPENSSL_SUPPORT` 移除重复的 target 级定义，仅保留全局
- **新增** — `OPENSSL_ROOT_DIR` 缓存路径存在性校验（跨机器共享 build 目录时自动清理）
- **新增** — `tests/test_deepseek_smoke.cpp` DeepSeek API 冒烟测试（手动执行，不在 CTest 中）
- **删除** — `docs/review/VOLUME_CHAPTER_FIELD_OVERLAP.md`（内容合并至 DEFERRED.md）
- 影响范围：`CMakeLists.txt`、`cmake/FetchDependencies.cmake`、`src/llm/`、`src/prompt/`、`src/config/`、`tests/`、`docs/review/`

## [2026-05-30] Phase 2 完成 — LLMClient + 测试全覆盖

- **新增** — `LLMClient` 类实现（Step 2.5），支持流式 `chat()` 和非流式 `chatNonStreaming()`
- **新增** — `StreamCallbacks` 回调结构体：on_content/on_reasoning/on_tool_call_start/on_complete/on_error
- **修改** — `Message::to_json` 修复：content 空 + tool_calls 非空 → null（OpenAI API 要求）
- **修改** — `StreamAccumulator` 新增 `completed_` 标志防止 [DONE] 二次触发覆盖 finish_reason
- **新增** — `test_sse_parser.cpp`（Step 2.6）：10 个 SSE 解析测试（token/tool_call/buffer/[DONE]/error）
- **新增** — `test_llm_client.cpp`（Step 2.7）：5 个 Mock HTTP 服务器测试（流式/非流式/401/缺 Key）
- **修改** — `PLAN.md` 更新至 v3.3，Phase 2 标记为已完成
- 测试统计：7 个可执行文件，36+ 测试点，ctest 100% 通过
- 影响范围：`src/llm/`、`tests/`、`CMakeLists.txt`、`PLAN.md`

## [2026-05-29] 流式架构重构 — StreamChunk + StreamAccumulator 职责分离

- **新增** — `Message.h` 中新增 `ToolCallDelta`、`UsageInfo`、`StreamChunk` 三个流式中间类型
- **修改** — `SSEParser` 简化为纯协议解析：`onChunk(StreamChunk)` 单回调替代 `onToken`/`onToolCall`/`onDone` 三回调，移除 `pending_tool_calls_` 和 `flushToolCalls()`
- **新增** — `StreamAccumulator` 类负责跨 chunk 合并（文本拼接 + tool_calls 按 index 累积 + 流结束时产出 `LLMResponse`）
- **修改** — `docs/review/REVIEW_NOTES.md` 清空（问题已移至 RESOLVED.md）
- 数据流：`SSE 文本 → SSEParser → StreamChunk → StreamAccumulator → LLMResponse`
- 影响范围：`src/llm/Message.h`、`src/llm/SSEParser.h`、`src/llm/SSEParser.cpp`、`src/llm/StreamAccumulator.h`、`src/llm/StreamAccumulator.cpp`、`CMakeLists.txt`

## [2026-05-29] 数据模型新增 Volume（卷纲）+ CharacterDevelopment（角色发展记录）+ 静态链接

- **新增** — `Volume` struct（14 字段）：卷级叙事弧线（title/summary/theme/goal/key_events 等），存储在 outline.json 内
- **新增** — `Chapter.volume_id` 字段 + `Outline.volumes[]`，章节可关联到所属卷
- **新增** — `CharacterDevelopment` struct（7 字段）：记录角色在剧情中的变化（外观/性格/能力等），支持按章节过滤和排序
- **新增** — `Character.development[]` 字段，可通过 `generation.exclude_fields = ["development"]` 整体控制
- **修改** — `format_version` 3→4（新增 Volume 向后兼容，旧项目 volumes 默认为空）
- **修改** — `PromptContextBuilder` 集成 Volume 上下文注入 + 角色发展记录过滤（按章节 order）+ 排序（chronological）+ GenerationControl 检查
- **修改** — `cmake/CompilerSettings.cmake` 添加 `-static-libgcc -static-libstdc++ -static`，exe 不再依赖外部 MinGW 运行时 DLL
- **新增** — 测试：`test_models` +4（Volume 往返/默认值/Outline 集成/Chapter.volume_id），+3（CharacterDevelopment 往返/默认值/Character 集成）
- **新增** — 测试：`test_prompt_context` +4（Volume 注入/不匹配告警/无 volume_id/order==0 + orphan 告警 + GenerationControl 排除）
- 影响范围：`src/project/Models.h`、`src/project/ProjectIO.h/.cpp`、`src/prompt/PromptContextBuilder.h/.cpp`、`cmake/CompilerSettings.cmake`、`tests/test_models.cpp`、`tests/test_prompt_context.cpp`

## [2026-05-28] 代码审查问题修复 — Message.h 完善 + SSEParser 流式合并 + 文档同步

- **新增** — `LLMResponse` 扩展 7 个字段：id, created, total_tokens, reasoning_content, cached_tokens, reasoning_tokens, system_fingerprint
- **新增** — `Message.h` 中 ToolCall/Message/ToolDefinition/LLMResponse 全部添加 `to_json`/`from_json`（与 Models.h 风格一致）
- **新增** — `roleToString`/`roleFromString` 辅助函数，MessageRole 枚举与 JSON 字符串互转
- **修改** — `SSEParser` 流式 tool_calls 按 index 累积合并，arguments 增量拼接，遇 finish_reason 触发完整回调
- **修改** — `TokenCounter::countMessages` 补充统计 `tool_call_id` 和 `name` 字段
- **修改** — `docs/PROJECT_ANALYSIS.md` + `docs/MODULES.md` 更新至 Phase 1 完成/Phase 2 进行中状态
- **修改** — `docs/REVIEW_NOTES.md` 修正 3 处不准确描述（#5 attributes 迁移/#9 命名空间/#10 测试列表）
- **删除** — `docs/CHANGELOG.md`（冗余，根目录 CHANGELOG.md 为唯一维护版本）
- 影响范围：`src/llm/Message.h`、`src/llm/SSEParser.h`、`src/llm/SSEParser.cpp`、`src/llm/TokenCounter.cpp`、`docs/`

## [2026-05-27] PLAN.md v3.1 — sqlite-vec 语义检索方案设计 + 常量注释补充

- **新增** — PLAN.md 依赖选择表加入 sqlite-vec（向量存储与 ANN 搜索，FetchContent 编译为静态库）
- **新增** — `src/retrieval/` 模块设计：VectorStore（sqlite-vec 封装）、EmbeddingGenerator（LLM embeddings API）、NovelChunker（场景边界智能切分）
- **新增** — 上下文管理策略新增"语义检索策略"章节：混合检索架构（确定性关联 + 语义检索）、嵌入内容策略表
- **新增** — Phase 4 从 5 步扩展到 9 步（Step 4.6-4.9：VectorStore → EmbeddingGenerator → NovelChunker → 混合检索集成）
- **新增** — 项目文件格式新增 `.novelagent/vectors.db`、技术风险表新增 sqlite-vec MinGW 兼容性风险
- **新增** — 测试计划新增 `test_retrieval.cpp`、CLI 斜杠命令新增 `/index`、`/search`
- **修改** — PLAN.md 版本号 3.0 → 3.1，步骤总数 31 → 35，Phase 4 标题改为"上下文管理与语义检索"
- **修改** — `Models.h` 和 `ProjectIO.cpp` 常量/类型别名补充中文注释（延续上一 commit 的注释全面补充工作）

## [2026-05-18] PLAN.md 同步更新至 v3.0

- **修改** — PLAN.md 全面刷新，标记 Phase 1 为已完成，反映实际超规格实现
- **修改** — 目录结构更新：新增 `prompt/`、`WorldRule`、当前测试文件清单
- **修改** — 数据模型章节新增：10 个 struct 设计说明 + GenerationControl 体系
- **修改** — Phase 4 步骤数减少（PromptContextBuilder 已提前落地）
- **修改** — Agent 工具新增 WorldRule CRUD、Phase 3 工具总数更新到 ~21 个
- **修改** — `.gitignore` 新增 `.cache/` clangd 索引目录

## [2026-05-18] 模型深化 — GenerationControl + Scene + WorldRule + PromptContextBuilder

- **新增** — `GenerationControl` 字段级提示词控制，每个 struct 自带 `generation` 字段
- **新增** — `Scene` 强类型结构体，替代 `vector<string>` 场景列表
- **新增** — `Relationship` 强类型结构体，替代 `map<string,string>` 角色关系
- **新增** — `WorldRule` 结构体 + `world_rules.json` 文件，独立规则建模
- **新增** — `PromptContextBuilder` 模块，按章节智能筛选上下文并渲染 LLM prompt
- **新增** — `shouldUseField()` 统一白名单/黑名单/标签过滤逻辑
- **修改** — 所有核心 struct 字段大幅扩展（Chapter/Character/Setting/PlotThread/Outline/Style/Project）
- **修改** — Setting 移除旧版 `attributes` 字段，相关迁移代码精简
- **修改** — Character 关系从 `map<string,string>` 迁移到 `vector<Relationship>`
- **修改** — `format_version` 提升到 3
- **修改** — `defaultNovelJson()` 改用 Project struct 构造，消除字段重复
- **注意** — 此为开发阶段破坏性变更，旧版项目 JSON 不兼容
- 影响范围：`src/project/Models.h`、`src/project/ProjectIO.cpp`、`src/prompt/`、`CMakeLists.txt`、`tests/`

## [2026-05-18] 数据模型重构 — tags + metadata 扩展

- **新增** — 所有核心 struct（Chapter/Character/Setting/Style/Project）增加 `tags` 和 `metadata` 字段
  - `tags`（`vector<string>`）用于轻量分类标签
  - `metadata`（`map<string, json>`）用于半结构化扩展，容纳未来创作元数据
- **新增** — `getMetadataWithUnknownKeys()` 机制，未知 JSON 字段自动收入 metadata
- **新增** — Setting 旧版 `attributes` → metadata 兼容迁移
- **新增** — `format_version` 从 1 提升到 2，`migrateProject()` 自动升级旧项目
- **新增** — `JsonUtils::getObjectOrEmpty()` 辅助函数
- **修改** — 手写 `to_json`/`from_json` 替代 `NLOHMANN_DEFINE_TYPE_INTRUSIVE`
- **修改** — utils 文件补充中文注释
- **修改** — 测试用例适配新字段，新增 `test_legacy_metadata_capture` 和 `test_legacy_load_migration`
- 影响范围：`src/project/Models.h`、`src/project/ProjectIO.cpp`、`src/utils/`、`tests/`

## [2026-05-17] 注释汉化

- **修改** — 所有源码注释从英文翻译为中文，符合项目注释语言规范
- **新增** — CLAUDE.md 项目指引文件
- 影响范围：`src/`、`tests/`、`CMakeLists.txt`、`.claude/settings.json`
