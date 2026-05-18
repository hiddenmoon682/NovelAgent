# NovelAgent CLI -- 细化实现计划

> 版本: 3.0 | 更新时间: 2026-05-18 | Phase 0 ✓ | Phase 1 ✓ | Phase 2 待实施

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
```

---

## 2. 依赖选择

| 库 | 用途 | 集成方式 | 理由 |
|---|------|---------|------|
| **nlohmann/json** | JSON 解析 | FetchContent (header-only) | 事实标准，最易用 |
| **CLI11** | CLI 参数解析 | FetchContent (header-only) | 轻量，子命令支持 |
| **spdlog** | 日志 | FetchContent (header-only) | 高性能，支持文件/控制台 |
| **cpp-httplib** | HTTP 请求 | FetchContent (header-only) | Windows 上走 WinHTTP，无外部依赖 |

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
Project (format_version=3)
├── Outline
│   ├── PlotThread[]
│   └── Chapter[]
│       └── Scene[]
├── Character[]
│   └── Relationship[]
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
| `PlotThread` | type, status, priority, stakes, central_question, resolution, start/end_chapter_id | 剧情线管理 |
| `Outline` | premise, story_structure, act_summaries | 大纲层次 |
| `Style` | voice_reference, show_vs_tell_bias, *_density, humor_level, sensory_focus, forbidden_phrases/tropes, chapter_opening/ending_style | 细粒度风格控制 |
| `Project` | logline, theme, central_question, target_audience, comps, content_rating, must_have/must_avoid_elements, narrative_promises, ending_type | 项目级元数据 |

---

## 5. 项目文件格式

```
my-novel/
  novel.json                # 项目顶层元数据
  outline.json              # 分层大纲 + PlotThread + Chapter
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

**为什么不用 RAG/向量检索？** 小说的数据是结构化的（章节、角色、设定），通过确定性 key 索引比语义检索更精确。MVP 阶段不需要引入向量数据库。

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

> 状态: **待实施** | 预计: 8 个步骤 | 依赖: Phase 0 + Phase 1

#### Step 2.1: 加入 cpp-httplib 依赖
**新建/修改**: `cmake/FetchDependencies.cmake`

- 添加 FetchContent 拉取 cpp-httplib
- 修改 CMakeLists.txt 链接 cpp-httplib

**验证**: 编译通过

#### Step 2.2: 定义 Message 结构体
**新建**: `src/llm/Message.h`

- `enum class MessageRole { System, User, Assistant, Tool }`
- `struct ToolCall { id, type, function_name, arguments }`
- `struct Message { role, content, tool_calls[], tool_call_id, name }`
- `struct ToolDefinition { name, description, parameters (json) }`
- `struct LLMResponse { content, tool_calls[], model, prompt_tokens, completion_tokens }`

**验证**: 编译通过

#### Step 2.3: 实现 TokenCounter
**新建**: `src/llm/TokenCounter.h`, `src/llm/TokenCounter.cpp`

- `countTokens(text)` — 中文字符 × 0.75 + 英文单词 × 1.3
- `countMessages(messages)` — 累计所有消息的 token 数
- `estimateChineseChars(text)` / `estimateEnglishWords(text)`
- 结果标注为"估算"，实际以 API 返回为准

**验证**: 编译通过

#### Step 2.4: 实现 SSEParser
**新建**: `src/llm/SSEParser.h`, `src/llm/SSEParser.cpp`

- `feed(data)` — 接收原始 SSE 数据块
- 解析 `data: {...}\n\n` 格式的行
- 提取 `choices[0].delta.content` → 输出 text token
- 提取 `choices[0].delta.tool_calls` → 输出 tool call 增量
- 处理 `[DONE]` 终止信号
- 处理跨数据块的不完整行（buffer 机制）

**验证**: 编译通过

#### Step 2.5: 实现 LLMClient
**新建**: `src/llm/LLMClient.h`, `src/llm/LLMClient.cpp`

- 构造函数接收 `ProviderConfig`
- `chat(messages, tools, system_prompt, callbacks)` → `LLMResponse`
- `buildRequestBody()` — 构造 JSON 请求体（model, messages, tools, stream, temperature 等）
- 发送 POST 到 `{base_url}/v1/chat/completions`
- 流式模式：通过 SSEParser 逐 token 回调 `on_token` 和 `on_tool_call`
- 非流式模式：解析完整 JSON 响应（用于 `--exec` 模式）
- 错误处理：网络错误、API 返回错误、超时

**验证**: 编译通过

#### Step 2.6: 写 SSE 解析测试
**新建**: `tests/test_sse_parser.cpp`, 更新 `tests/CMakeLists.txt`

- 测试单个 token delta 解析
- 测试多个 token 在同一 data 行
- 测试 tool_call delta 解析
- 测试跨 buffer 的不完整行
- 测试 [DONE] 终止
- 测试空 data 行

**运行**: `ctest` 通过

#### Step 2.7: 写 LLMClient Mock 测试
**新建**: `tests/test_llm_client.cpp`

- 启动本地 HTTP mock 服务器，返回预设 SSE 响应
- 测试 chat() 的 token 流式回调
- 测试 chat() 的 tool_call 回调
- 测试错误响应处理
- 测试 API key 缺失时的行为

**运行**: 全部测试通过

#### Step 2.8: 提交 Phase 2
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

### Phase 4: 上下文管理

> 状态: **待实施** | 预计: 5 个步骤 | 依赖: Phase 3
> 注：PromptContextBuilder 已在 Phase 1 提前落地，Phase 4 专注在预算分配和降级策略

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
    │                           ├── Phase 3 (Agent + REPL) ── Phase 4 (上下文管理) ── Phase 5 (打磨)
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

## 各 Phase 步骤总览

| Phase | 步骤数 | 状态 |
|-------|--------|------|
| Phase 0 | — | ✓ 完成 |
| Phase 1 | — | ✓ 完成（超规格） |
| Phase 2 | 8 steps | ○ 待实施 |
| Phase 3 | 12 steps | ○ 待实施 |
| Phase 4 | 5 steps | ○ 待实施（PromptContextBuilder 已提前落地） |
| Phase 5 | 6 steps | ○ 待实施 |
| **总计** | **31 steps** | Phase 0-1 done, 31 remaining |
