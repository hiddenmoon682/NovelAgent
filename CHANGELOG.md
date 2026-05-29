# Changelog

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
