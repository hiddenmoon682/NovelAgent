# Changelog

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
