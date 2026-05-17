# NovelAgent CLI -- 细化实现计划

> 版本: 2.0 | 更新时间: 2026-05-17 | Phase 0 ✓ 完成

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
- 更新 `CHANGELOG.md`，详细记录该阶段做了什么
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

  cmake/
    FetchDependencies.cmake       # FetchContent 拉取依赖
    CompilerSettings.cmake        # 编译选项、警告

  docs/                           # 项目文档
    MODULES.md                    # 模块介绍
    PROJECT_ANALYSIS.md           # 项目分析报告

  src/
    main.cpp                      # 入口，CLI 参数解析，分发

    cli/
      ReplHandler.h / .cpp        # 交互式 REPL 循环
      StreamDisplay.h / .cpp      # 终端输出：流式 token、颜色
      CommandParser.h / .cpp      # 斜杠命令解析

    llm/
      Message.h                   # Message, ToolCall, ToolDefinition 数据结构
      LLMClient.h / .cpp          # HTTP + OpenAI 兼容 API 客户端
      SSEParser.h / .cpp          # SSE 流式解析
      TokenCounter.h / .cpp       # Token 估算

    project/
      Models.h                    # Project, Chapter, Character, Setting, Outline
      ProjectIO.h / .cpp          # 磁盘读写
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

    config/
      AppConfig.h / .cpp          # 全局配置

    utils/
      FileUtils.h / .cpp
      StringUtils.h / .cpp
      JsonUtils.h / .cpp

  tests/
    CMakeLists.txt
    test_main.cpp                 # [Phase 0] smoke test
    test_project_io.cpp           # [Phase 1]
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

## 4. 上下文管理策略

### 预算分配
- 系统提示词 + 工具定义：~1500 tokens（固定）
- 输出预留：上下文窗口的 20%
- 剩余可变预算：
  - **50%** — 当前章节全文 + 大纲 + 场景角色信息
  - **30%** — 最近对话（原始消息）
  - **20%** — 历史压缩摘要

### 降级策略
1. 截断当前章节到末尾 2000 字
2. 移除角色详细档案（LLM 改用 `get_character` 工具查询）
3. 移除相邻章节大纲
4. 截断对话到最近 5 轮
5. 全文压缩为摘要

**为什么不用 RAG/向量检索？** 小说的数据是结构化的（章节、角色、设定），通过确定性 key 索引比语义检索更精确。MVP 阶段不需要引入向量数据库。

---

## 5. 项目文件格式

```
my-novel/
  novel.json              # 项目元数据
  outline.json            # 层级章节大纲
  characters.json         # 角色档案
  settings.json           # 世界观设定
  style.json              # 写作风格配置
  chapters/
    001-introduction.md
    ...
  .novelagent/
    conversation.json     # 完整对话历史
    summaries.json        # 章节摘要缓存
    state.json            # Agent 状态
```

---

## 6. CLI 系统

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
| `get_project_status` / `search_novel` / `count_words` / `update_style_config` | 项目操作 |

---

## 7. Agent 核心循环

```
用户输入 → 追加到对话历史
         → ContextManager 组装上下文
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

## 8. 分阶段实现（细化版）

---

### Phase 0: 项目骨架 ✓ 已完成

> 状态: **Done** | 提交: `53ac257`

产物：CMakeLists.txt, FetchDependencies, CompilerSettings, main.cpp (CLI11 参数解析),
AppConfig (JSON 读写 + 环境变量), FileUtils, StringUtils, JsonUtils, smoke test。

---

### Phase 1: 数据模型 + 项目 I/O

> 状态: **待实施** | 预计: 8 个步骤 | 依赖: Phase 0

#### Step 1.1: 完善 Models.h — 全部数据结构
**新建/修改**: `src/project/Models.h`

- 定义 `Chapter` struct（id, title, order, synopsis, scenes[], status, word_count, file_path, 等）
- 定义 `Character` struct（id, name, role, appearance, personality, background, traits[], relationships{}, chapter_appearances[], arc）
- 定义 `Setting` struct（id, name, category, description, attributes{}, notes）
- 定义 `Outline` struct（premise, plot_threads[], chapters[]）
- 定义 `Style` struct（tone, pacing, pov, tense, prose_style, dialogue_style, chapter_length_target 等）
- 扩展已有 `Project` struct（增加 author, description, genre[], target_word_count, current_word_count, status, pov, tense, created, modified 字段）
- 所有 struct 使用 `NLOHMANN_DEFINE_TYPE_INTRUSIVE` 宏自动生成 JSON 序列化

**验证**: 编译通过 + 测试用例 `test_models.cpp` 通过

#### Step 1.2: 编写 Models 测试 — JSON 序列化往返
**新建**: `tests/test_models.cpp`, 更新 `tests/CMakeLists.txt`

- 测试 Chapter 的 JSON 序列化/反序列化往返
- 测试 Character（含 relationships map）的往返
- 测试 Setting（含 attributes map）的往返
- 测试 Outline（含嵌套 Chapter 和 PlotThread）的往返
- 测试 Style 的往返
- 测试 Project 的 to_json/from_json（path 字段不被序列化）
- 测试空 vector/map 字段的反序列化

**运行**: `ctest` 通过

#### Step 1.3: 实现 ProjectIO — 核心文件读写
**新建**: `src/project/ProjectIO.h`, `src/project/ProjectIO.cpp`

- `load(path)` — 从目录加载 `novel.json` + `outline.json` + `characters.json` + `settings.json` + `style.json`
- `save(project)` — 保存所有 JSON 文件到项目目录
- `readChapter(project, chapter_id)` — 读取 `chapters/{id}.md`
- `writeChapter(chapter_id, content)` — 写入章节 Markdown 文件
- `loadConversation(path)` — 加载对话历史
- `appendConversation(path, msg)` — 追加一条消息
- `createProjectDir(path, title)` — 创建完整的项目目录骨架

**验证**: 编译通过

#### Step 1.4: 实现 ProjectManager — 项目生命周期
**修改**: `src/project/ProjectManager.h`, `src/project/ProjectManager.cpp`

- `create(path, title)` — 创建新项目（调用 ProjectIO::createProjectDir）
- `open(path)` — 打开已有项目（调用 ProjectIO::load）
- `openOrCreate(path)` — 自动判断
- `listProjects(baseDir)` — 列举 `~/.novelagent/projects/` 下的所有项目
- `getDefaultProjectDir(title)` — 根据标题生成目录名

#### Step 1.5: 更新 CMakeLists.txt — 加入 project/ 文件
**修改**: `CMakeLists.txt`

- 将 `ProjectIO.h/.cpp` 加入 SOURCES
- 确认 Models.h, ProjectManager 已在列表中

#### Step 1.6: 写 ProjectIO 测试 — 往返一致性
**新建/修改**: `tests/test_project_io.cpp`, `tests/CMakeLists.txt`

- 创建临时项目目录
- 写入 Project → 保存 → 重新加载 → 验证所有字段一致
- 写入章节 → 读取 → 验证内容一致
- 对话历史追加 → 加载 → 验证消息顺序
- 清理临时目录

**运行**: `ctest` 通过

#### Step 1.7: 写 ProjectManager 测试 — 生命周期
**修改**: `tests/test_project_io.cpp` 或在同一文件中扩展

- 测试 create → 目录结构完整
- 测试 open → 正确加载已有项目
- 测试 openOrCreate → 新建和打开均正确
- 测试 listProjects → 正确列出多个项目

**运行**: 全部测试通过

#### Step 1.8: 集成到 main.cpp — 项目命令行操作
**修改**: `src/main.cpp`

- `-p` 指定项目路径时，用 ProjectManager 打开/创建
- 启动时显示项目摘要（标题、字数、章节数）
- `-e` 模式先加载项目再执行（Phase 3 时完整实现）

**验证**: `./novelagent.exe -p examples/test-novel` 显示项目信息

#### Step 1.9: 提交 Phase 1
- 更新 CHANGELOG.md 记录 Phase 1 所有变更
- `git commit` + `git push`

---

### Phase 2: LLM 客户端

> 状态: **待实施** | 预计: 8 个步骤 | 依赖: Phase 0（与 Phase 1 可并行）

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
- 更新 CHANGELOG.md
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

**验证**: 编译通过

#### Step 3.2: 实现 Agent 核心循环
**新建**: `src/agent/Agent.h`, `src/agent/Agent.cpp`

- `processUserMessage(input)` → `LLMResponse`
- 维护 `conversation_` 历史
- 调用 ContextManager 组装上下文
- 调用 LLMClient.chat()
- tool call 循环（最多 10 次）：
  1. 收到响应，检查 tool_calls
  2. 如有：执行工具 → 追加结果到对话 → 再次调用 LLM
  3. 如无：退出循环
- `execute(command)` — 单次命令模式

**验证**: 编译通过

#### Step 3.3: 实现 ContextManager (基础版)
**新建**: `src/agent/ContextManager.h`, `src/agent/ContextManager.cpp`

- `assemble(conversation, chapter_id, context_window)` → `ContextAssembly`
- `buildSystemPrompt(project)` → 构造系统提示词
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
- `update_character` — 修改已有角色

#### Step 3.6: 实现 Setting 工具
**新建**: `src/agent/tools/SettingTools.cpp`

- `get_setting(id)` / `get_settings()` — 按类别过滤
- `update_setting` — 更新或创建设定

#### Step 3.7: 实现 Outline 工具 + Project 工具
**新建**: `src/agent/tools/OutlineTools.cpp`, `src/agent/tools/ProjectTools.cpp`

- Outline: `get_outline`, `update_outline`, `set_premise`
- Project: `get_project_status`, `search_novel`, `count_words`, `update_style_config`

#### Step 3.8: 注册所有工具到 ToolRegistry
**新建/修改**: Agent 初始化代码

- 在 Agent 构造函数中注册所有 18 个工具
- 每个工具提供 JSON Schema 参数定义
- 配置工具描述（告诉 LLM 何时使用）

#### Step 3.9: 实现 ReplHandler (完整版)
**修改**: `src/cli/ReplHandler.h`, `src/cli/ReplHandler.cpp`

- 主循环：`std::getline` 读输入
- 以 `/` 开头 → 拦截走 CommandParser
- 其他 → 调用 Agent::processUserMessage()
- `run()` — 启动 REPL，显示欢迎信息

#### Step 3.10: 实现 CommandParser
**新建**: `src/cli/CommandParser.h`, `src/cli/CommandParser.cpp`

- 解析斜杠命令，返回命令类型 + 参数
- 支持的命令（Phase 3 MVP）：
  - `/help` — 显示所有可用命令
  - `/save` — 调用 ProjectIO::save
  - `/load <path>` — 切换项目
  - `/clear` — 清空对话历史
  - `/model <provider> <model>` — 切换 LLM 配置
  - `/exit` — 退出程序

#### Step 3.11: 实现 StreamDisplay
**新建**: `src/cli/StreamDisplay.h`, `src/cli/StreamDisplay.cpp`

- `beginResponse()` / `streamToken(text)` / `endResponse()`
- `showToolCall(tool_call)` — 灰色标注
- `showToolResult(summary)` — 简短结果
- `showStatus(model, tokens)` — 状态行

#### Step 3.12: 端到端集成测试
**修改**: `src/main.cpp`

- 连接 Agent + ReplHandler + ProjectManager + LLMClient
- `novelagent -p test-novel` → 进入 REPL
- 手动测试：创建角色 → 写大纲 → 写第一章
- `novelagent -p test-novel -e "列出所有章节"` → 单次命令

#### Step 3.13: 写 ToolRegistry 测试
**新建**: `tests/test_tool_registry.cpp`

- 注册/分发/错误处理测试
- Mock ProjectIO 测试工具函数

#### Step 3.14: 提交 Phase 3
- 更新 CHANGELOG.md
- `git commit` + `git push`

---

### Phase 4: 上下文管理

> 状态: **待实施** | 预计: 6 个步骤 | 依赖: Phase 3

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

#### Step 4.5: 写上下文管理测试
**新建**: `tests/test_context_manager.cpp`

- 测试 token 预算计算
- 测试超预算截断
- 测试多级降级顺序
- 测试摘要读写

#### Step 4.6: 提交 Phase 4
- 更新 CHANGELOG.md
- `git commit` + `git push`

---

### Phase 5: 打磨

> 状态: **待实施** | 预计: 7 个步骤 | 依赖: Phase 3 + Phase 4

#### Step 5.1: ANSI 颜色输出
**修改**: `src/cli/StreamDisplay.cpp`

- 助手回复：绿色 (`\033[32m`)
- 用户输入：蓝色 (`\033[34m`)
- 工具调用：灰色 (`\033[90m`)
- 错误信息：红色 (`\033[31m`)
- 在 Windows 上启用 ANSI 支持（`SetConsoleMode`）

#### Step 5.2: 斜杠命令补全
**修改**: `src/cli/ReplHandler.cpp`

- Tab 补全斜杠命令名
- 输入 `/` 后按 Tab 列出所有命令

#### Step 5.3: 剩余的斜杠命令
**修改**: `src/cli/CommandParser.cpp`

- `/status` — 显示项目统计
- `/config <key> <value>` — 运行时修改配置
- `/export` — 导出所有章节为单个 Markdown 文件

#### Step 5.4: 错误恢复
**修改**: `src/main.cpp`, `src/agent/Agent.cpp`

- 未捕获异常自动保存项目
- LLM 调用超时的重试逻辑（最多 3 次）
- 磁盘写入失败的友好提示

#### Step 5.5: Markdown 渲染（基础）
**修改**: `src/cli/StreamDisplay.cpp`

- 检测 LLM 输出中的 Markdown 格式（`**粗体**`, `*斜体*`, 代码块）
- 用 ANSI 转义码渲染到终端

#### Step 5.6: 写集成测试脚本
**新建**: 测试脚本或手动 checklist

- 完整流程测试：创建项目 → 构建大纲 → 创建角色 → 写章节 → 导出
- 中文显示正常（无乱码）
- 上下文降级后不丢关键信息

#### Step 5.7: 最终提交 Phase 5 + 版本发布
- 更新 CHANGELOG.md 归档
- 更新 README.md 作为项目入口文档
- `git tag v0.1.0`
- `git commit` + `git push --tags`

---

## 依赖阶段图

```
Phase 0 (骨架) ✓ 完成
    │
    ├── Phase 1 (数据模型 I/O) ──┐
    │                             ├── Phase 3 (Agent + REPL) ── Phase 4 (上下文) ── Phase 5 (打磨)
    └── Phase 2 (LLM 客户端) ────┘
         (Phase 1 和 2 可并行)
```

## 关键技术风险

| 风险 | 缓解措施 |
|------|---------|
| cpp-httplib 在 MinGW WinHTTP 兼容性 | Phase 2 Step 2.1 先验证编译；失败则只走非流式 HTTP |
| 中文 Token 估算不准确 | 标注为估算值，以 API 返回为准 |
| Console 中文显示 | 启用 UTF-8 codepage，使用 ANSI 转义码 |
| API Key 泄露到 git | config 文件已在 .gitignore 中 |

## 验证方法总览

| Phase | 验证手段 |
|-------|---------|
| Phase 0 | 编译 + smoke test 通过 |
| Phase 1 | ProjectIO 往返测试 + ProjectManager 生命周期测试 |
| Phase 2 | SSE 解析测试 + Mock HTTP 测试 |
| Phase 3 | 真实 API 端到端："创建角色 → 写大纲 → 写第一章" |
| Phase 4 | 超长对话（200+ 轮）上下文降级测试 |
| Phase 5 | Windows Terminal 颜色/中文验证 + 导出文件检查 |

---

## 各 Phase 步骤总览

| Phase | 步骤数 | 状态 |
|-------|--------|------|
| Phase 0 | — | ✓ 完成 |
| Phase 1 | 9 steps | ○ 待实施 |
| Phase 2 | 8 steps | ○ 待实施 |
| Phase 3 | 14 steps | ○ 待实施 |
| Phase 4 | 6 steps | ○ 待实施 |
| Phase 5 | 7 steps | ○ 待实施 |
| **总计** | **44 steps** | 8/44 done |
