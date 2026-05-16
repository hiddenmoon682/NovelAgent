# NovelAgent 模块介绍

> 更新时间: 2026-05-17 | 当前版本: v0.1.0 (Phase 0)

---

## 整体架构

```
                    ┌─────────────────┐
                    │    main.cpp      │
                    │  CLI11 参数解析   │
                    └────────┬────────┘
                             │
              ┌──────────────┼──────────────┐
              │              │               │
     ┌────────▼───┐  ┌──────▼──────┐  ┌─────▼────┐
     │    cli/     │  │   agent/     │  │ config/  │
     │ REPL 交互   │  │ Agent 核心   │  │ 配置管理  │
     └────────────┘  └──────┬──────┘  └──────────┘
                             │
              ┌──────────────┼──────────────┐
              │              │               │
     ┌────────▼───┐  ┌──────▼──────┐  ┌─────▼────┐
     │   llm/      │  │  project/   │  │  utils/  │
     │ LLM 客户端  │  │ 数据模型I/O │  │ 工具库    │
     └────────────┘  └────────────┘  └──────────┘
```

---

## 模块清单

### 1. `src/main.cpp` — 程序入口

| 属性 | 说明 |
|------|------|
| 当前状态 | ✓ Phase 0 完成 |
| 文件数 | 1 (`main.cpp`) |
| 依赖 | config/, cli/, project/, CLI11, spdlog |

**职责**: 程序入口点。解析命令行参数，初始化配置，分发到对应运行模式。

**两种运行模式**:
- **交互式 REPL** (`novelagent -p my-novel`) — 进入对话循环
- **单次命令** (`novelagent -p my-novel -e "写第三章"`) — 执行后退出

**支持的参数**: `-p/--project`, `-e/--exec`, `--provider`, `-v/--verbose`

**启动流程**:
1. CLI11 解析命令行参数
2. 加载 `~/.novelagent/config.json` 配置
3. 从环境变量注入 API Key (优先级高于配置文件)
4. 打开或创建小说项目目录
5. 进入 REPL (Phase 3 实现)

---

### 2. `src/config/` — 配置管理

| 属性 | 说明 |
|------|------|
| 当前状态 | ✓ Phase 0 完成 |
| 文件数 | 2 (`AppConfig.h`, `AppConfig.cpp`) |
| 依赖 | utils/, nlohmann/json, spdlog |

**职责**: 管理 LLM Provider 配置和 API Key。支持 JSON 持久化，支持环境变量注入。

**核心类型**:

```
ProviderConfig              AppConfig
├── name: string            ├── default_provider: string
├── api_key: string         ├── providers: map<name, ProviderConfig>
├── base_url: string        ├── load()
├── model: string           ├── loadFromFile(path)
├── context_window: int     ├── save(path)
├── temperature: double     ├── getProvider(name) → ProviderConfig*
└── max_tokens: int         └── setApiKey(provider, key)
```

**配置优先级**: 环境变量 (`DEEPSEEK_API_KEY` 等) > 配置文件 (`~/.novelagent/config.json`)

**设计要点**: 所有三个 LLM provider (DeepSeek, Kimi, Claude) 使用同一个 `ProviderConfig` 结构，因为它们的 API 格式都是 OpenAI 兼容的。只有 `base_url` 和 `model` 名称不同。

---

### 3. `src/utils/` — 工具库

| 属性 | 说明 |
|------|------|
| 当前状态 | ✓ Phase 0 完成 |
| 文件数 | 4 (`FileUtils.h/.cpp`, `StringUtils.h`, `JsonUtils.h`) |
| 依赖 | std::filesystem, nlohmann/json |

**职责**: 为所有其他模块提供底层工具函数，封装常见操作。

| 子模块 | 内容 |
|--------|------|
| **FileUtils** | 文件读写、目录操作、路径拼接、`homeDir()`、`configDir()` |
| **StringUtils** | trim、split、join、startsWith/endsWith、大小写转换 |
| **JsonUtils** | `getOpt<T>()` 安全取值 (返回 optional)、`getOrDefault<T>()` 带默认值 |

**设计要点**: 所有函数都放在 `utils::file` / `utils::string` / `utils::json` 命名空间下，通过命名空间前缀避免全局污染。

---

### 4. `src/project/` — 数据模型与项目 I/O

| 属性 | 说明 |
|------|------|
| 当前状态 | ○ Phase 1 待实现 (仅 stub) |
| 规划文件 | `Models.h`, `ProjectIO.h/.cpp`, `ProjectManager.h/.cpp` |
| 依赖 | utils/, nlohmann/json |

**职责**: 定义小说的完整数据模型，负责项目目录的创建、加载、保存。这是整个系统的数据层。

**核心数据结构** (Phase 1 实现):

```
Project                   — 项目元数据 (标题、作者、字数、风格)
  ├── Outline             — 层级大纲 (前提 → 情节线 → 章节 → 场景)
  │    └── Chapter[]      — 单章大纲 (标题、摘要、场景列表、POV角色)
  ├── Character[]         — 角色档案 (姓名、外貌、性格、关系、弧线)
  ├── Setting[]           — 世界观设定 (位置、建筑、规则)
  └── Style               — 写作风格 (语气、节奏、文风、对话风格)
```

**项目目录结构**:
```
my-novel/
  novel.json              # Project 元数据
  outline.json            # Outline
  characters.json         # Character[]
  settings.json           # Setting[]
  style.json              # Style
  chapters/
    001-chapter.md         # 章节用 Markdown，可手动编辑
  .novelagent/
    conversation.json      # 对话历史
    summaries.json         # 摘要缓存
    state.json             # 运行状态
```

**ProjectIO** — 负责与磁盘交互:
- `load(path)` / `save(project)` — 整体加载/保存
- `readChapter(id)` / `writeChapter(id, content)` — 章节读写
- `loadConversation()` / `appendConversation(msg)` — 对话历史

**ProjectManager** — 负责项目生命周期:
- `openOrCreate(path)` — 打开或创建项目
- `isValid(path)` — 验证项目完整性

---

### 5. `src/llm/` — LLM 客户端

| 属性 | 说明 |
|------|------|
| 当前状态 | ○ Phase 2 待实现 |
| 规划文件 | `Message.h`, `LLMClient.h/.cpp`, `SSEParser.h/.cpp`, `TokenCounter.h/.cpp` |
| 依赖 | cpp-httplib (HTTP), nlohmann/json |

**职责**: 封装与 LLM API 的通信。是系统与外部 AI 服务的唯一桥梁。

**核心类型**:

```
Message                     LLMClient
├── role: enum              ├── ProviderConfig
├── content: string         ├── chat(params) → LLMResponse
├── tool_calls: vector       │   ├── stream: true
└── tool_call_id: optional  │   ├── on_token callback
                             │   └── on_tool_call callback
ToolCall                    └── buildRequestBody()
├── id: string
├── function_name: string   SSEParser
└── arguments: json_string  ├── feed(data)
                             └── emits: token, tool_call, done
ToolDefinition
├── name: string            TokenCounter
├── description: string     ├── count(text) → int
└── parameters: json_schema └── 中文字数 × 0.75, 英文 × 0.25
```

**设计要点**:
- **不使用 Provider 继承** — DeepSeek、Kimi、Claude 都兼容 OpenAI 格式，只需一个 `LLMClient` 类，用 `ProviderConfig` 区分
- **流式输出** — POST 请求带 `stream: true`，通过 SSE (Server-Sent Events) 逐 token 返回，`on_token` 回调实时推送给终端显示
- **HTTP 库** — 使用 `cpp-httplib` (header-only)，Windows 上走 WinHTTP 无需额外依赖

**请求流程**:
```
POST {base_url}/v1/chat/completions
  → SSE stream
    → parse "data: {...}" lines
      → extract delta.content → on_token callback
      → extract delta.tool_calls → on_tool_call callback
  → [DONE] event → 返回 LLMResponse
```

---

### 6. `src/agent/` — Agent 核心

| 属性 | 说明 |
|------|------|
| 当前状态 | ○ Phase 3 待实现 |
| 规划文件 | `Agent.h/.cpp`, `ToolRegistry.h/.cpp`, `ContextManager.h/.cpp`, `tools/*.cpp` |
| 依赖 | llm/, project/, cli/ |

**职责**: 整个系统的"大脑"。将用户输入、上下文管理、LLM 调用、工具执行串联成一个完整的 Agent 循环。

#### 6.1 Agent (核心循环)

```
用户输入
  ↓
Agent.processUserMessage(input)
  ↓
追加到 conversation_history
  ↓
┌─ ContextManager.assemble(history, chapter) ───┐
│  裁切上下文窗口，组装 system prompt + messages  │
└───────────────────────────────────────────────┘
  ↓
┌─ Tool Loop (最多 10 次) ─────────────────────┐
│  LLMClient.chat(messages, tools)              │
│  如果有 tool_calls → executeTools → 继续循环  │
│  如果没有 → 显示回复 → 退出循环               │
└──────────────────────────────────────────────┘
  ↓
追加 assistant 回复到 conversation_history
  ↓
返回 LLMResponse
```

#### 6.2 ToolRegistry (工具注册)

管理 LLM 可调用的工具。每个工具包含:
- `name` — snake_case 标识符
- `description` — 告诉 LLM 何时使用
- `parameters` — JSON Schema 描述入参
- `function` — 实际的 C++ 回调

**工具模块分工**:

| 文件 | 包含的工具 |
|------|-----------|
| `tools/ChapterTools.cpp` | `read_chapter`, `write_chapter`, `create_chapter`, `append_to_chapter`, `list_chapters` |
| `tools/CharacterTools.cpp` | `get_character`, `get_characters`, `create_character`, `update_character` |
| `tools/SettingTools.cpp` | `get_setting`, `get_settings`, `update_setting` |
| `tools/OutlineTools.cpp` | `get_outline`, `update_outline`, `set_premise` |
| `tools/ProjectTools.cpp` | `get_project_status`, `search_novel`, `count_words`, `update_style_config` |

#### 6.3 ContextManager (上下文管理)

**Phase 3 基础版**: 仅做截断，按 token 预算裁剪最旧的消息

**Phase 4 完整版**: 多级压缩策略
- 50% 预算给当前章节全文 + 大纲 + 在场角色
- 30% 给最近对话 (完整保留)
- 20% 给历史压缩摘要 (旧对话 → 摘要)
- 超出预算时逐级降级: 截断章节 → 移除角色详情 → 移除相邻大纲 → 截断对话 → 全压缩

---

### 7. `src/cli/` — CLI 交互层

| 属性 | 说明 |
|------|------|
| 当前状态 | ○ Phase 3 待实现 (仅 ReplHandler stub) |
| 规划文件 | `ReplHandler.h/.cpp`, `CommandParser.h/.cpp`, `StreamDisplay.h/.cpp` |
| 依赖 | agent/, utils/ |

**职责**: 用户界面层。读取用户输入、显示 AI 回复、处理斜杠命令。

#### 7.1 ReplHandler (REPL 循环)

主循环通过 `std::getline` 读取用户输入:
- 以 `/` 开头 → 拦截为斜杠命令，本地处理，不发给 LLM
- 其他 → 发送给 Agent 处理

**斜杠命令**:

| 命令 | 功能 | 实现难度 |
|------|------|---------|
| `/help` | 显示帮助信息 | 低 |
| `/save` | 保存当前项目到磁盘 | 低 |
| `/load <path>` | 切换到指定项目 | 中 |
| `/clear` | 清空对话历史 (保留项目数据) | 低 |
| `/model <provider> <model>` | 切换 LLM 提供者 | 低 |
| `/status` | 显示项目统计 (字数、章节数) | 低 |
| `/config <key> <value>` | 修改配置项 | 中 |
| `/export` | 导出全部章节为单个 Markdown | 中 |

#### 7.2 StreamDisplay (流式显示)

LLM 回复是逐 token 流式返回的，本模块负责:
- **Assistant 回复** — 逐 token 实时输出 (绿色文本)
- **Tool Call** — 灰色小字标注 "正在调用工具..."
- **Tool Result** — 显示工具返回的简短摘要
- **状态行** — 底部显示当前模型、token 使用量

#### 7.3 CommandParser (命令解析)

解析单次命令模式 (`-e "写第三章的第一场戏"`) 的输入，将其转换为 Agent 可处理的消息格式。

---

## 模块依赖关系

```
                    ┌──────────┐
                    │   main   │
                    └────┬─────┘
                         │
         ┌───────────────┼───────────────┐
         │               │               │
    ┌────▼────┐    ┌─────▼─────┐    ┌───▼───┐
    │  config │    │    cli    │    │project│
    └────┬────┘    └─────┬─────┘    └───┬───┘
         │               │               │
         │          ┌────▼────┐          │
         │          │  agent  │          │
         │          └────┬────┘          │
         │               │               │
         │     ┌─────────┼─────────┐     │
         │     │         │         │     │
    ┌────▼──┐ ┌▼───┐ ┌───▼──┐ ┌──▼─────▼──┐
    │ utils │ │llm │ │project│ │agent/tools│
    └───────┘ └────┘ └───────┘ └───────────┘
```

**依赖说明**:
- `utils/` 是最底层，被所有模块依赖
- `config/` 只依赖 `utils/`，独立于其他业务模块
- `llm/` 和 `project/` 互相独立，可并行开发
- `agent/` 同时依赖 `llm/` 和 `project/` (以及 `agent/tools/`)
- `cli/` 依赖 `agent/` 和 `project/`
- `agent/tools/` 依赖 `project/` (通过 ProjectIO 读写数据)

---

## 实现进度

| 模块 | Phase | 状态 | 完成度 |
|------|-------|------|--------|
| main.cpp | 0 | ✓ | CLI参数解析、配置加载、环境变量注入 |
| config/ | 0 | ✓ | ProviderConfig、AppConfig、JSON 持久化 |
| utils/ | 0 | ✓ | FileUtils、StringUtils、JsonUtils |
| project/ | 1 | ○ | 仅 Project stub，待实现完整数据模型 |
| llm/ | 2 | ○ | 待实现 HTTP、SSE、Token 计数 |
| agent/ | 3 | ○ | 待实现 Agent 循环、工具注册 |
| agent/tools/ | 3 | ○ | 待实现 18 个工具 |
| cli/ | 3 | ○ | 仅 ReplHandler stub，待实现完整 REPL |
