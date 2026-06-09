# Changelog

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
- **新增** — 上下文管理策略新增"语义检索策略"章节：混合检索架构（确定性关联 + 语义检索）、三层相关性排序、嵌入内容策略表
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
