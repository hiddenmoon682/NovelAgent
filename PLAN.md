# NovelAgent CLI -- 细化实现计划

> 版本: 3.3 | 更新时间: 2026-05-30 | Phase 0 ✓ | Phase 1 ✓ | Phase 2 ✓ | Phase 3 待实施

## 背景

构建一个 AI 辅助写小说的工具。第一阶段是类似 Claude Code 的 CLI 交互式工具，后续扩展到 GUI/Web。
技术栈：CLI 阶段用 C++20 + MinGW64，后续页面用其他语言。LLM 使用 DeepSeek / Kimi / Claude API。

---

## 项目规则

### 规则 1: 中文注释
- 所有注释、文档、CHANGELOG 均使用**中文**编写
- 代码标识符（变量名、函数名、类型名）仍使用英文

### 规则 2: 每步骤必测
- 每完成一个 Step 后，不仅要编译通过，还必须编写对应的测试用例
- 测试用例统一放在 `tests/` 目录下，命名格式：`test_<模块名>.cpp`
- 测试覆盖：正常路径、边界条件、错误处理
- 验证标准：`ctest` 全部通过才算该 Step 完成

### 规则 3: 每 Phase 结束后的操作
- 更新 `CHANGELOG.md`，增量记录（新条目加到最上面，不删除旧记录）
- `git commit` 提交该阶段所有变更
- `git push` 推送到 GitHub

---

## 1. 项目目录结构

```
D:\C++Code\C++NovelAgent\
  CMakeLists.txt
  .gitignore
  CHANGELOG.md
  PLAN.md
  CLAUDE.md

  cmake/
    FetchDependencies.cmake       # FetchContent 拉取依赖
    CompilerSettings.cmake        # 编译选项、警告

  docs/
    MODULES.md                    # 模块介绍
    PROJECT_ANALYSIS.md           # 项目分析报告

  src/
    main.cpp                      # 入口，CLI 参数解析，分发

    cli/
      ReplHandler.h / .cpp        # 交互式 REPL 循环（Phase 3 完整实现）

    llm/
      Message.h                   # Message, ToolCall, ToolDefinition 数据结构
      LLMClient.h / .cpp          # HTTP + OpenAI 兼容 API 客户端
      SSEParser.h / .cpp          # SSE 流式解析
      TokenCounter.h / .cpp       # Token 估算

    prompt/                       # [Phase 1 提前落地]
      PromptContextBuilder.h/.cpp # 按章节筛选上下文 + 渲染 LLM prompt

    retrieval/                    # [Phase 4 新增] 语义检索管线
      VectorStore.h / .cpp        # sqlite-vec 封装：向量存储、ANN 搜索、CRUD
      EmbeddingGenerator.h/.cpp   # 调用 LLM API embedding endpoint 生成向量
      NovelChunker.h / .cpp       # 按场景/角色/设定边界智能切分小说文本

    project/
      Models.h                    # 10 个 struct：Project/Outline/PlotThread/Chapter/
                                  #   Scene/Character/Relationship/Setting/WorldRule/Style
                                  #   + GenerationControl + shouldUseField()
      ProjectIO.h / .cpp          # 磁盘读写（7 个 JSON 文件 + 章节 Markdown）
      ProjectManager.h / .cpp     # 创建、打开、列举项目

    agent/
      Agent.h / .cpp              # 核心 Agent 循环
      ToolRegistry.h / .cpp       # 工具注册、描述、调度
      ContextManager.h / .cpp     # 上下文窗口预算和组装
      tools/
        ChapterTools.cpp
        CharacterTools.cpp
        SettingTools.cpp
        OutlineTools.cpp
        ProjectTools.cpp
        WorldRuleTools.cpp        # 世界规则 CRUD（配合 WorldRule）

    config/
      AppConfig.h / .cpp          # 全局配置

    utils/
      FileUtils.h / .cpp
      StringUtils.h / .cpp
      JsonUtils.h / .cpp

  tests/
    CMakeLists.txt
    test_main.cpp                 # [Phase 0] smoke test
    test_models.cpp               # [Phase 1] 10 struct 序列化往返 + 旧版兼容
    test_project_io.cpp           # [Phase 1] ProjectIO + ProjectManager
    test_prompt_context.cpp       # [Phase 1] PromptContextBuilder
    test_sse_parser.cpp           # [Phase 2]
    test_llm_client.cpp           # [Phase 2]
    test_tool_registry.cpp        # [Phase 3]
    test_context_manager.cpp      # [Phase 4]
    test_retrieval.cpp            # [Phase 4]
```

---

## 2. 依赖选择

| 库 | 用途 | 集成方式 | 理由 |
|---|------|---------|------|
| **nlohmann/json** | JSON 解析 | FetchContent (header-only) | 事实标准，最易用 |
| **CLI11** | CLI 参数解析 | FetchContent (header-only) | 轻量，子命令支持 |
| **spdlog** | 日志 | FetchContent (header-only) | 高性能，支持文件/控制台 |
| **cpp-httplib** | HTTP 请求 | FetchContent (header-only) | Windows 上走 WinHTTP，无外部依赖 |
| **sqlite-vec** | 向量存储与 ANN 搜索 | FetchContent (编译为静态库) | 零依赖 SQLite 扩展，单文件存储，万级向量毫秒检索 |

> Phase 0 实际验证后，从 libcurl 切换到 cpp-httplib——header-only，无需系统安装 MSYS2 包。

**REPL 方案**：使用 `std::getline` + 简单自定义实现，避免在 MinGW 上引入 replxx 的编译兼容性问题。后续可升级为 replxx。

---

## 3. LLM Provider 抽象

单 `LLMClient` 类 + `ProviderConfig` 参数化（三家 API 均兼容 OpenAI 格式）：

```cpp
struct ProviderConfig {
    std::string name;          // "deepseek", "kimi", "claude"
    std::string api_key;
    std::string base_url;      // "https://api.deepseek.com"
    std::string model;         // "deepseek-chat"
    int context_window = 65536;
    double temperature = 0.7;
};
```

API Key 优先级：环境变量 (`DEEPSEEK_API_KEY` 等) > `~/.novelagent/config.json`

---

## 4. 数据模型（Phase 1 已实现）

### 设计原则
- **稳定核心字段 + 半结构化扩展**：常用字段保持强类型，扩展字段走 `tags` + `metadata`
- **GenerationControl**：每个 struct 自带字段级提示词控制，`shouldUseField()` 统一过滤
- **未知字段自动吸收**：`getMetadataWithUnknownKeys()` 确保 JSON 中未定义的键不会加载时丢失

### 结构层次
```
Project (format_version=4)
├── Outline
│   ├── Volume[]
│   ├── PlotThread[]
│   └── Chapter[]
│       └── Scene[]
├── Character[]
│   ├── Relationship[]
│   └── CharacterDevelopment[]
├── Setting[]
├── WorldRule[]
└── Style
```

### 各 struct 要点

| Struct | 关键字段 | 说明 |
|--------|---------|------|
| `GenerationControl` | enabled, include/exclude_fields, required/blocked_tags, prompt_hint | 字段级提示词控制 |
| `Scene` | goal, conflict, outcome, turning_point, emotional_beat, reveal, foreshadowing, payoff, pov_character_id, location_id, participants | 章节内最小戏剧单元 |
| `Relationship` | target_character_id, type, public_status, private_feeling, tension | 结构化角色关系 |
| `Chapter` | goal, conflict, outcome, hook, reveal, foreshadowing, payoff, emotional_beat, location_id, active_plot_threads, focus_characters/ settings, scenes[] | 扩展了叙事节奏字段 |
| `Character` | goal, motivation, internal/external_conflict, secret, fear, misbelief, speaking_style, core_values, taboos, relationships[] | 深度角色建模 |
| `Setting` | story_function, sensory_profile, related_characters/plot_threads/rule_ids | 叙事功能导向 |
| `WorldRule` | summary, limitations, costs, exceptions, known_by, related_settings | 世界规则一致性 |
| `Volume` | title, order, summary, theme, goal, start/end_chapter_id, key_events, focus_characters, active_plot_threads | 卷级叙事弧线（2026-05-29 新增） |
| `CharacterDevelopment` | chapter_id, summary, category, affected_fields | 角色变化追踪，按章节过滤排序（2026-05-29 新增） |
| `PlotThread` | type, status, priority, stakes, central_question, resolution, start/end_chapter_id | 剧情线管理 |
| `Outline` | premise, story_structure, act_summaries | 大纲层次 |
| `Style` | voice_reference, show_vs_tell_bias, *_density, humor_level, sensory_focus, forbidden_phrases/tropes, chapter_opening/ending_style | 细粒度风格控制 |
| `Project` | logline, theme, central_question, target_audience, comps, content_rating, must_have/must_avoid_elements, narrative_promises, ending_type | 项目级元数据 |

---

## 5. 项目文件格式

```
my-novel/
  novel.json                # 项目顶层元数据
  outline.json              # 分层大纲 + Volume + PlotThread + Chapter
  characters.json           # 角色档案（含 Relationship）
  settings.json             # 世界观设定
  world_rules.json          # [v3 新增] 世界规则
  style.json                # 写作风格配置
  chapters/
    001-introduction.md
    ...
  .novelagent/
    conversation.json       # 完整对话历史
    summaries.json          # 章节摘要缓存
    state.json              # Agent 状态
    vectors.db              # [Phase 4] sqlite-vec 向量存储
```

---

## 6. 上下文管理策略

### 已有的基础设施（PromptContextBuilder）
- 按章节 ID 筛选关联的角色/设定/剧情线/世界规则
- `filterObject()` 按 `GenerationControl` 对字段做白名单/黑名单过滤
- 产出结构化 `payload` + 可发送给 LLM 的 `rendered_prompt`

### 预算分配（Phase 4 完善）
- 系统提示词 + 工具定义：~1500 tokens（固定）
- 输出预留：上下文窗口的 20%
- 剩余可变预算：
  - **50%** — 当前章节全文 + 大纲 + 场景角色信息
  - **30%** — 最近对话（原始消息）
  - **20%** — 历史压缩摘要

### 降级策略（Phase 4 实现）
1. 截断当前章节到末尾 2000 字
2. 移除角色详细档案（LLM 改用 `get_character` 工具查询）
3. 移除相邻章节大纲
4. 截断对话到最近 5 轮
5. 全文压缩为摘要

### 语义检索策略（Phase 4 新增）

**为什么需要向量检索？** 在长篇网络小说场景（1000+ 章、数百万字），确定性 ID 遍历面临三个瓶颈：
1. **关联爆炸**：一个贯穿全程的角色可能出现在数百章中，全部塞入 prompt 会超出 token 预算
2. **相关性无法排序**：ID 遍历知道"哪些实体有关联"，但不知道"在当前场景下哪些最相关"
3. **跨章节内容检索**：作者问"我之前写过某个角色用某种方式破解了阵法，是哪一章？"——这是全文语义搜索，不是实体查询

**方案选型：sqlite-vec**
- 编译为静态库链接进二进制，零运维负担——用户无需安装任何数据库
- 向量数据存储在项目 `.novelagent/vectors.db` 中，随项目目录可打包分享
- 对于 400 万字小说（约 2 万条向量），ANN 搜索延迟 < 1ms
- 上限百万级向量，远超网文场景需求（《从零开始》2000 万字级别也只需 ~10 万条）
- 相比完整向量数据库（Milvus/Qdrant/Weaviate），无需独立服务进程，无需用户配置

**混合检索架构：**
```
用户查询 / 当前写作上下文
        │
        ├── 确定性关联（PromptContextBuilder）
        │     └── 按 chapter_id 遍历 ID 关系图 → 精确召回关联实体
        │
        └── 语义检索（VectorStore）
              └── 查询向量 → ANN 搜索 → Top-K 语义相似内容
                    │
                    └── 合并、去重、按相关性排序
                          │
                          └── 注入 ContextManager 的 token 预算分配
```

**三层相关性排序：**
1. **确定性关联**（权重 0.5）：ID 关系图遍历——角色在哪些章节出现、情节线关联了哪些设定
2. **启发式排序**（权重 0.2）：出现频率、最近活跃度、情节线优先级——不依赖向量
3. **语义相似度**（权重 0.3）：嵌入向量余弦相似度——在前两层产生过多候选时做最终截断

**嵌入内容策略：**

| 嵌入对象 | 切分粒度 | 元数据 | 更新时机 |
|---------|---------|--------|---------|
| 章节正文 | 按场景边界切分（500-2000 字/块，相邻块 10% 重叠） | chapter_id, scene_index, pov_character | 章节写入后增量更新 |
| 角色描述 | 每个角色一条（goal + motivation + traits + speaking_style + conflict） | character_id, role_type | 角色创建/更新后 |
| 设定描述 | 每个设定一条（description + sensory_profile + story_function） | setting_id, category | 设定创建/更新后 |
| 世界规则 | 每条规则一条（summary + limitations + costs + exceptions） | rule_id, known_by | 规则创建/更新后 |

**嵌入生成：** 复用 LLM Provider 的 embeddings endpoint（如 DeepSeek `v1/embeddings`），与 Chat API 共用 API Key 和 base_url，无需额外配置。400 万字一次性嵌入约 ¥8（DeepSeek 当前价格），日常增量更新几乎无成本。

**检索触发时机：**
- 用户显式 `/search <query>` 斜杠命令
- Agent 工具 `search_novel` 调用（VectorStore 作为其实现后端，替代原计划的全文遍历）
- ContextManager 自动补全：当确定性关联的实体数超过 token 预算阈值时，用语义相关性排序截断

---

## 7. CLI 系统

### 斜杠命令（本地处理）
| 命令 | 功能 |
|------|------|
| `/help` | 显示帮助 |
| `/save` | 保存项目 |
| `/load <path>` | 切换项目 |
| `/clear` | 清空对话 |
| `/model <provider> <model>` | 切换 LLM |
| `/status` | 项目统计 |
| `/config <key> <value>` | 修改配置 |
| `/export` | 导出 Markdown |

### Agent 工具（LLM function calling 调用）
| 工具 | 功能 |
|------|------|
| `read_chapter` / `write_chapter` / `create_chapter` / `append_to_chapter` / `list_chapters` | 章节操作 |
| `get_outline` / `update_outline` | 大纲操作 |
| `get_character` / `get_characters` / `create_character` / `update_character` | 角色操作 |
| `get_settings` / `get_setting` / `update_setting` | 设定操作 |
| `get_world_rule` / `get_world_rules` / `update_world_rule` | 世界规则操作 |
| `get_project_status` / `search_novel` / `count_words` / `update_style_config` | 项目操作 |

---

## 8. Agent 核心循环

```
用户输入 → 追加到对话历史
         → ContextManager 组装上下文（调用 PromptContextBuilder）
         → LLMClient 发送请求（流式输出显示）
         → 如果有 tool_calls：
              → 执行工具调用
              → 将结果追加到对话
              → 重新发送给 LLM
              → 最多循环 10 次
         → 如果没有 tool_calls：
              → 显示完整回复
              → 追加到对话历史
```

---

## 9. 分阶段实现

---

### Phase 0: 项目骨架 ✓ 已完成

> 状态: **Done** | 提交: `53ac257`

产物：CMakeLists.txt, FetchDependencies, CompilerSettings, main.cpp (CLI11 参数解析),
AppConfig (JSON 读写 + 环境变量), FileUtils, StringUtils, JsonUtils, smoke test。

---

### Phase 1: 数据模型 + 项目 I/O ✓ 已完成

> 状态: **Done** | 提交: `2acd8a7` | 超规格完成

**实际实现内容（超出原始计划）**：

- `Models.h` — 10 个 struct，全部手写 `to_json`/`from_json`（非宏），支持 `tags`/`metadata`/`GenerationControl`
- `Scene` / `Relationship` / `WorldRule` 为开发过程中衍生出的新 struct（原计划没有）
- `GenerationControl` + `shouldUseField()` 字段级提示词控制（原计划没有，属于 Phase 4 上下文管理的提前落地）
- `ProjectIO` — 7 个 JSON 文件 + 章节 Markdown 读写，`format_version=3`
- `ProjectManager` — 项目生命周期管理
- `PromptContextBuilder` — 按章节智能筛选上下文 + 渲染 LLM prompt（原计划属于 Phase 4，提前实现）
- 测试：`test_models`（序列化往返 + 旧版兼容）、`test_project_io`、`test_prompt_context`

**与原始计划的差异**：
- 模型规模从 6 个 struct 扩展到 10 个，字段数增长约 3x
- `GenerationControl` 的设计消除了 Phase 4 中上下文过滤的硬编码逻辑
- Setting 旧版 `attributes` 字段已移除，相关兼容迁移代码已精简
- Character 关系从 `map<string,string>` 升级到 `vector<Relationship>`

---

### Phase 2: LLM 客户端

> 状态: **已完成** | 预计: 8 个步骤 | 依赖: Phase 0 + Phase 1

#### Step 2.1: 加入 cpp-httplib 依赖 ✓ 已完成
**新建/修改**: `cmake/FetchDependencies.cmake`, `CMakeLists.txt`

- 添加 FetchContent 拉取 cpp-httplib v0.18.5（header-only，Windows 上封装 WinHTTP）
- 修改 CMakeLists.txt 链接 `httplib::httplib`

**验证**: 编译通过，4 个测试全部通过

#### Step 2.2: 定义 Message 结构体 ✓ 已完成
**新建**: `src/llm/Message.h`

- `enum class MessageRole { System, User, Assistant, Tool }` — OpenAI 标准四角色
- `struct ToolCall { id, type, function_name, arguments }` — LLM 工具调用
- `struct Message { role, content, tool_calls[], tool_call_id, name }` — 单条消息
- `struct ToolDefinition { name, description, parameters (json) }` — 工具注册定义
- `struct LLMResponse { content, tool_calls[], model, prompt_tokens, completion_tokens }` — API 返回

**验证**: 编译通过，4/4 测试无回归

#### Step 2.3: 实现 TokenCounter ✓ 已完成
**新建**: `src/llm/TokenCounter.h`, `src/llm/TokenCounter.cpp`

- `countTokens(text)` — 中文 × 0.75 + 英文单词 × 1.3 启发式估算
- `countMessages(messages)` — 累计消息 token（含角色标记 + 工具调用结构开销）
- CJK 检测覆盖基本块 + Ext-A/B + 兼容区（U+4E00~9FFF, 3400~4DBF, F900~FAFF, 20000~2A6DF）
- 结果标注为估算值，实际以 API `usage` 字段为准

**验证**: 编译通过，4/4 测试无回归

#### Step 2.4: 实现 SSEParser ✓ 已完成
**新建**: `src/llm/SSEParser.h`, `src/llm/SSEParser.cpp`

- `feed(data)` — 按双换行切分事件，buffer 机制处理跨数据块断行
- 解析 `choices[0].delta.content` → `on_token` 回调
- 解析 `choices[0].delta.tool_calls` → `on_tool_call` 回调
- `[DONE]` 终止信号 → `on_done` 回调
- JSON 解析异常 → `on_error` 回调
- 回调通过 `std::function` 注入，SSEParser 不持有对话状态

**验证**: 编译通过，5/5 测试无回归

#### Step 2.5: 实现 LLMClient ✓ 已完成
**新建**: `src/llm/LLMClient.h`, `src/llm/LLMClient.cpp`

- 构造函数接收 `ProviderConfig`
- `chat(messages, tools, system_prompt, callbacks)` → `LLMResponse`
- `buildRequestBody()` — 构造 JSON 请求体（model, messages, tools, stream, temperature 等）
- 发送 POST 到 `{base_url}/v1/chat/completions`
- 流式模式：通过 SSEParser 逐 token 回调 `on_token` 和 `on_tool_call`
- 非流式模式：解析完整 JSON 响应（用于 `--exec` 模式）
- 错误处理：网络错误、API 返回错误、超时

**验证**: 编译通过

#### Step 2.6: 写 SSE 解析测试 ✓ 已完成
**新建**: `tests/test_sse_parser.cpp`, 更新 `tests/CMakeLists.txt`

- 测试单个 token delta 解析
- 测试多个 token 在同一 data 行
- 测试 tool_call delta 解析
- 测试跨 buffer 的不完整行
- 测试 [DONE] 终止
- 测试空 data 行

**运行**: `ctest` 通过

#### Step 2.7: 写 LLMClient Mock 测试 ✓ 已完成
**新建**: `tests/test_llm_client.cpp`

- 启动本地 HTTP mock 服务器，返回预设 SSE 响应
- 测试 chat() 的 token 流式回调
- 测试 chat() 的 tool_call 回调
- 测试错误响应处理
- 测试 API key 缺失时的行为

**运行**: 全部测试通过

#### Step 2.8: 提交 Phase 2 ✓ 已完成
- 更新 CHANGELOG.md（增量）
- `git commit` + `git push`

---

### Phase 3: Agent + REPL MVP

> 状态: **待实施** | 预计: 12 个步骤 | 依赖: Phase 1 + Phase 2

#### Step 3.1: 实现 ToolRegistry
**新建**: `src/agent/ToolRegistry.h`, `src/agent/ToolRegistry.cpp`

- `registerTool(name, description, json_schema, callback_fn)`
- `getToolDefinitions()` → `vector<ToolDefinition>`（发给 LLM 的工具列表）
- `executeTool(name, args_json)` → 执行并返回 JSON 结果
- 错误处理：工具不存在、参数解析失败、回调异常
- 工具数量：原计划 18 个，加上 WorldRule 工具约 21 个

**验证**: 编译通过

#### Step 3.2: 实现 Agent 核心循环
**新建**: `src/agent/Agent.h`, `src/agent/Agent.cpp`

- `processUserMessage(input)` → `LLMResponse`
- 维护 `conversation_` 历史
- 调用 ContextManager 组装上下文（集成 PromptContextBuilder）
- 调用 LLMClient.chat()
- tool call 循环（最多 10 次）：
  1. 收到响应，检查 tool_calls
  2. 如有：执行工具 → 追加结果到对话 → 再次调用 LLM
  3. 如无：退出循环
- `execute(command)` — 单次命令模式

**验证**: 编译通过

#### Step 3.3: 实现 ContextManager（基础版）
**新建**: `src/agent/ContextManager.h`, `src/agent/ContextManager.cpp`

- `assemble(conversation, chapter_id, context_window)` → `ContextAssembly`
- `buildSystemPrompt(project)` → 构造系统提示词（利用 PromptContextBuilder）
- `calculateBudget(window_size)` → 计算 token 预算
- **基础版**：仅截断 `conversation` 中超出预算的最旧消息
- 暂不做摘要压缩（留到 Phase 4）

**验证**: 编译通过

#### Step 3.4: 实现 Chapter 工具
**新建**: `src/agent/tools/ChapterTools.cpp`

- `read_chapter` — 通过 ProjectIO::readChapter 读取
- `write_chapter` — 通过 ProjectIO::writeChapter 写入
- `create_chapter` — 创建新文件 + 更新 outline
- `append_to_chapter` — 读取 → 追加 → 写回
- `list_chapters` — 列出所有章节 ID + 标题 + 状态

#### Step 3.5: 实现 Character 工具
**新建**: `src/agent/tools/CharacterTools.cpp`

- `get_character(id)` / `get_characters()` — 从 project 查询
- `create_character` — 添加到 characters.json
- `update_character` — 修改已有角色（含 Relationship 管理）

#### Step 3.6: 实现 Setting + WorldRule 工具
**新建**: `src/agent/tools/SettingTools.cpp`, `src/agent/tools/WorldRuleTools.cpp`

- Setting: `get_setting(id)` / `get_settings()` / `update_setting`
- WorldRule: `get_world_rule(id)` / `get_world_rules()` / `update_world_rule`

#### Step 3.7: 实现 Outline 工具 + Project 工具
**新建**: `src/agent/tools/OutlineTools.cpp`, `src/agent/tools/ProjectTools.cpp`

- Outline: `get_outline`, `update_outline`, `set_premise`
- Project: `get_project_status`, `search_novel`, `count_words`, `update_style_config`

#### Step 3.8: 注册所有工具到 ToolRegistry
**新建/修改**: Agent 初始化代码

- 在 Agent 构造函数中注册所有工具（约 21 个）
- 每个工具提供 JSON Schema 参数定义
- 配置工具描述（告诉 LLM 何时使用）

#### Step 3.9: 实现 ReplHandler（完整版）
**修改**: `src/cli/ReplHandler.h`, `src/cli/ReplHandler.cpp`

- 主循环：`std::getline` 读输入
- 以 `/` 开头 → 拦截走 CommandParser
- 其他 → 调用 Agent::processUserMessage()
- `run()` — 启动 REPL，显示欢迎信息

#### Step 3.10: 实现 CommandParser + StreamDisplay
**新建**: `src/cli/CommandParser.h/.cpp`, `src/cli/StreamDisplay.h/.cpp`

- CommandParser: `/help`, `/save`, `/load`, `/clear`, `/model`, `/exit`
- StreamDisplay: 流式 token 输出、工具调用灰色标注、状态行

#### Step 3.11: 端到端集成测试
**修改**: `src/main.cpp`

- 连接 Agent + ReplHandler + ProjectManager + LLMClient
- `novelagent -p test-novel` → 进入 REPL
- 手动测试：创建角色 → 写大纲 → 写第一章
- `novelagent -p test-novel -e "列出所有章节"` → 单次命令

#### Step 3.12: 提交 Phase 3
- 更新 CHANGELOG.md（增量）
- `git commit` + `git push`

---

### Phase 4: 上下文管理与语义检索

> 状态: **待实施** | 预计: 9 个步骤 | 依赖: Phase 2 + Phase 3
> 注：PromptContextBuilder 已在 Phase 1 提前落地，Phase 4 包含上下文预算/降级（Step 4.1-4.5）和语义检索管线（Step 4.6-4.9）

#### Step 4.1: 实现对话历史摘要
**修改**: `src/agent/ContextManager.cpp`

- `summarizeConversation(messages)` — 提取关键信息压缩为摘要
- 摘要策略：保留角色名、剧情要点、当前任务，去掉闲聊和技术细节
- 摘要存储在 memory 中（不调用 LLM 做摘要，用规则提取）

#### Step 4.2: 实现章节摘要
**修改**: `src/agent/ContextManager.cpp`

- `getChapterSummary(chapter_id)` — 从 `.novelagent/summaries.json` 读取
- `updateChapterSummary(chapter_id, content)` — 提取关键词和要点保存
- 摘要包含：该章节的关键事件、出场角色、场景变化

#### Step 4.3: 实现多级降级
**修改**: `src/agent/ContextManager.cpp`

- 实现完整的 `assemble()` 预算分配（50/30/20）
- 实现 5 级降级策略
- 添加预算日志（spdlog::debug 输出分配明细）

#### Step 4.4: 实现会话持久化
**修改**: `src/project/ProjectIO.cpp`, `src/agent/ContextManager.cpp`

- 程序退出时自动保存对话历史
- 程序启动时自动加载上次对话
- `/clear` 命令保存旧对话到归档文件

#### Step 4.5: 写上下文管理测试 + 提交
**新建**: `tests/test_context_manager.cpp`

- 测试 token 预算计算、超预算截断、多级降级顺序、摘要读写

#### Step 4.6: 引入 sqlite-vec 依赖 + 实现 VectorStore
**新建**: `src/retrieval/VectorStore.h`, `src/retrieval/VectorStore.cpp`
**修改**: `cmake/FetchDependencies.cmake`, `CMakeLists.txt`

- FetchContent 拉取 sqlite-vec，编译为静态库链接
- `VectorStore` 类封装：
  - `init(db_path)` — 打开/创建 `.novelagent/vectors.db`，建表和向量索引
  - `insert(id, embedding, metadata_json)` — 插入向量 + 元数据
  - `search(embedding, top_k)` → `vector<SearchResult>` — ANN 搜索，返回 id + 相似度 + 元数据
  - `delete(id)` / `update(id, embedding)` — 更新/删除
  - `count()` → `int` — 向量总数
- 向量维度从 EmbeddingGenerator 获取（通常 1024 或 1536）
- 表结构：`vec_items(id TEXT PRIMARY KEY, embedding BLOB, metadata TEXT)` + virtual table for vector index
- `SearchResult` 结构：`{ id, similarity, metadata_json }`

**验证**: 编译通过，VectorStore 单元测试（插入/搜索/删除/更新）

#### Step 4.7: 实现 EmbeddingGenerator
**新建**: `src/retrieval/EmbeddingGenerator.h`, `src/retrieval/EmbeddingGenerator.cpp`

- 构造函数接收 `ProviderConfig`（复用 LLM 配置的 base_url + api_key）
- `generateEmbedding(text)` → `vector<float>` — 单条文本嵌入
- `generateEmbeddings(texts)` → `vector<vector<float>>` — 批量嵌入（减少 API 调用次数）
- 调用 `POST {base_url}/v1/embeddings`（OpenAI 兼容格式，DeepSeek/Kimi 均支持）
- 请求体：`{ model: "text-embedding-3-small", input: text }`
- 错误处理：API 错误、超时重试、返回维度校验
- 可选方案（远期）：本地 ONNX Runtime + all-MiniLM-L6-v2，离线免费用，但集成复杂度较高

**验证**: 编译通过，Mock 测试 embedding 生成和批量请求

#### Step 4.8: 实现 NovelChunker
**新建**: `src/retrieval/NovelChunker.h`, `src/retrieval/NovelChunker.cpp`

- `chunkChapter(chapter, markdown_content)` → `vector<TextChunk>`
  - 优先按 Scene 边界切分（复用 Models.h 中已定义的 Scene 结构）
  - 若无 Scene 信息，按段落/空行边界切分
  - 每个 chunk 500-2000 字，在段落边界处切断，避免截断对话或动作描写
  - 相邻 chunk 保留 10-20% 重叠（维持语义连贯性，提高检索召回率）
- `chunkCharacter(character)` → `string` — 角色核心信息拼接为单条可嵌入文本
- `chunkSetting(setting)` → `string` — 设定信息拼接
- `chunkWorldRule(rule)` → `string` — 世界规则拼接
- `TextChunk` 结构：`{ id, text, metadata{type, source_id, chapter_id, chunk_index} }`

**验证**: 编译通过，切分逻辑单元测试（中文文本边界、空章节、无 Scene 退化为段落切分）

#### Step 4.9: 集成混合检索 + 测试 + 提交
**修改**: `src/agent/ContextManager.cpp`, `src/agent/tools/ProjectTools.cpp`

- `ContextManager::assemble()` 集成 VectorStore 语义搜索作为上下文补充来源
- `search_novel` 工具改为调用 `VectorStore::search()` 实现（替代原计划的全文字符串遍历）
- 实现三层相关性融合排序：确定性关联(0.5) + 启发式(0.2) + 语义相似度(0.3)
- 添加 `/index` 斜杠命令：手动触发全量嵌入生成（遍历所有章节+角色+设定+规则）
- 增量索引：章节写入后自动为新内容生成嵌入并插入 VectorStore

**新建**: `tests/test_retrieval.cpp`

- 测试 VectorStore 插入/搜索/删除/更新
- 测试 NovelChunker 切分中文小说文本（Markdown 格式、含场景标记）
- 测试 EmbeddingGenerator mock（请求体格式验证、批量拆分）
- 测试混合检索融合排序（确定性 + 语义结果去重合并）

**运行**: `ctest` 全部通过
- 更新 CHANGELOG.md（增量）
- `git commit` + `git push`

---

### Phase 5: 打磨

> 状态: **待实施** | 预计: 6 个步骤 | 依赖: Phase 3 + Phase 4

#### Step 5.1: ANSI 颜色输出
**修改**: `src/cli/StreamDisplay.cpp`

- 助手回复：绿色、用户输入：蓝色、工具调用：灰色、错误信息：红色
- 在 Windows 上启用 ANSI 支持（`SetConsoleMode`）

#### Step 5.2: Tab 补全 + 剩余斜杠命令
**修改**: `src/cli/ReplHandler.cpp`, `src/cli/CommandParser.cpp`

- Tab 补全斜杠命令名
- `/status` — 显示项目统计
- `/config <key> <value>` — 运行时修改配置
- `/export` — 导出所有章节为单个 Markdown 文件

#### Step 5.3: 错误恢复
**修改**: `src/main.cpp`, `src/agent/Agent.cpp`

- 未捕获异常自动保存项目
- LLM 调用超时的重试逻辑（最多 3 次）
- 磁盘写入失败的友好提示

#### Step 5.4: Markdown 渲染（基础）
**修改**: `src/cli/StreamDisplay.cpp`

- 检测 LLM 输出中的 Markdown 格式（`**粗体**`, `*斜体*`, 代码块）
- 用 ANSI 转义码渲染到终端

#### Step 5.5: 集成测试
**新建**: 完整流程测试 checklist

- 完整流程测试：创建项目 → 构建大纲 → 创建角色 → 写章节 → 导出
- 中文显示正常（无乱码）
- 上下文降级后不丢关键信息

#### Step 5.6: 最终提交 Phase 5 + 版本发布
- 更新 CHANGELOG.md 归档
- 更新 README.md 作为项目入口文档
- `git tag v0.1.0`
- `git commit` + `git push --tags`

---

## 依赖阶段图

```
Phase 0 (骨架) ✓ 完成
    │
    ├── Phase 1 (数据模型 I/O) ✓ 完成
    │     │
    │     └── PromptContextBuilder（Phase 4 基础设施提前落地）
    │
    ├── Phase 2 (LLM 客户端) ──┐
    │                           ├── Phase 3 (Agent + REPL) ── Phase 4 (上下文管理 + 语义检索) ── Phase 5 (打磨)
    └───────────────────────────┘
```

## 关键技术风险

| 风险 | 缓解措施 |
|------|---------|
| cpp-httplib 在 MinGW WinHTTP 兼容性 | Phase 2 Step 2.1 先验证编译；失败则只走非流式 HTTP |
| 中文 Token 估算不准确 | 标注为估算值，以 API 返回为准 |
| Console 中文显示 | 启用 UTF-8 codepage，使用 ANSI 转义码 |
| API Key 泄露到 git | config 文件已在 .gitignore 中 |
| 模型字段持续膨胀 | GenerationControl + metadata 机制已就位，可吸收大部分扩展需求 |
| sqlite-vec 在 MinGW 编译兼容性 | Phase 4 Step 4.6 先验证编译；失败则退回到暴力遍历 + JSON 文件存储向量 |

## 各 Phase 步骤总览

| Phase | 步骤数 | 状态 |
|-------|--------|------|
| Phase 0 | — | ✓ 完成 |
| Phase 1 | — | ✓ 完成（超规格） |
| Phase 2 | 8 steps | ✓ 已完成 |
| Phase 3 | 12 steps | ○ 待实施 |
| Phase 4 | 9 steps | ○ 待实施（PromptContextBuilder 已提前落地） |
| Phase 5 | 6 steps | ○ 待实施 |
| **总计** | **35 steps** | Phase 0-1 done, 35 remaining |
