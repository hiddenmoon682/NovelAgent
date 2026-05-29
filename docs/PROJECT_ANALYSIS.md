# NovelAgent 项目分析报告

> 生成日期: 2026-05-28 | 当前阶段: Phase 1 完成, Phase 2 进行中

---

## 1. 项目概览

**NovelAgent** 是一个 AI 辅助长篇小说写作工具，类似 Claude Code 的交互式 CLI。

| 属性 | 值 |
|------|-----|
| 语言 | C++20 |
| 构建 | CMake 4.3 + MinGW64 g++ 13.1.0 |
| 平台 | Windows (主), 跨平台兼容 |
| 仓库 | https://github.com/hiddenmoon682/NovelAgent |
| 当前版本 | v0.1.0 |

**两阶段产品路线:**
- **第一阶段**: CLI 交互式工具 (当前)
- **第二阶段**: GUI 客户端 / Web 应用 (后续)

---

## 2. 目录结构

```
NovelAgent/
├── CMakeLists.txt                  # 根构建文件
├── PLAN.md                         # 完整实现计划
├── CHANGELOG.md                    # 阶段变更记录
├── .gitignore
│
├── cmake/
│   ├── CompilerSettings.cmake      # MSVC/GCC 编译选项
│   └── FetchDependencies.cmake     # FetchContent 拉取依赖
│
├── src/
│   ├── main.cpp                    # 入口 (CLI11 参数解析)
│   │
│   ├── config/                     # [Phase 0 ✓] 配置管理
│   │   ├── AppConfig.h             # ProviderConfig + AppConfig 数据结构
│   │   └── AppConfig.cpp           # JSON 读写, env var 注入
│   │
│   ├── utils/                      # [Phase 0 ✓] 工具库
│   │   ├── FileUtils.h/.cpp        # std::filesystem 封装
│   │   ├── StringUtils.h           # trim/split/join/大小写
│   │   └── JsonUtils.h             # nlohmann::json 安全取值
│   │
│   ├── project/                    # [Phase 1 ✓] 数据模型 + I/O
│   │   ├── Models.h                # 10 个 struct, GenerationControl, to_json/from_json
│   │   ├── ProjectManager.h/.cpp   # 当前: openOrCreate stub
│   │   └── ProjectIO.h/.cpp        # 项目 JSON 文件读写, load/save, 对话历史
│   │
│   ├── llm/                        # [Phase 2 ○] LLM 客户端
│   │   ├── Message.h               # Message/ToolCall/ToolDefinition/LLMResponse 数据结构
│   │   ├── LLMClient.h/.cpp        # 待实现
│   │   ├── SSEParser.h/.cpp        # SSE 流式解析, tool_calls 按 index 合并
│   │   └── TokenCounter.h/.cpp     # 启发式 token 计数（中英文混合）
│   │
│   ├── agent/                      # [Phase 3 待] Agent 核心
│   │   ├── Agent.h/.cpp            # 待实现
│   │   ├── ToolRegistry.h/.cpp     # 待实现
│   │   ├── ContextManager.h/.cpp   # 待实现
│   │   └── tools/                  # 待实现
│   │       ├── ChapterTools.cpp
│   │       ├── CharacterTools.cpp
│   │       ├── SettingTools.cpp
│   │       ├── OutlineTools.cpp
│   │       └── ProjectTools.cpp
│   │
│   └── cli/                        # [Phase 3 待] CLI 交互
│       ├── ReplHandler.h/.cpp      # 当前: run() stub
│       ├── CommandParser.h/.cpp    # 待实现
│       └── StreamDisplay.h/.cpp    # 待实现
│
├── tests/
│   ├── CMakeLists.txt
│   └── test_main.cpp               # nlohmann/json smoke test
│
└── docs/
    └── PROJECT_ANALYSIS.md          # 本文档
```

---

## 3. 模块完成度总览

| 模块 | Phase | 状态 | 完成度 |
|------|-------|------|--------|
| 构建系统 | 0 | ✓ 完成 | CMakeLists.txt, 依赖拉取, 编译通过 |
| 配置管理 | 0 | ✓ 完成 | AppConfig, ProviderConfig, JSON 持久化 |
| 文件工具 | 0 | ✓ 完成 | FileUtils, StringUtils, JsonUtils |
| CLI 入口 | 0 | ✓ 完成 | CLI11 参数解析, --help 自动生成 |
| 数据模型 | 1 | ✓ 完成 | 10 个 struct, GenerationControl, PromptContextBuilder |
| 项目 I/O | 1 | ✓ 完成 | ProjectIO load/save, 对话历史, 章节读写 |
| LLM 客户端 | 2 | ○ 进行中 | Message/ToolCall/LLMResponse 数据结构 + SSEParser + TokenCounter |
| Agent 核心 | 3 | ○ 待实施 | Agent 循环, 工具注册 |
| 上下文管理 | 4 | ○ 待实施 | 摘要, 压缩, 降级 |
| CLI 交互 | 3 | ○ 待实施 | REPL 循环, 斜杠命令, 流式显示 |
| 打磨 | 5 | ○ 待实施 | 颜色, 补全, 导出 |

---

## 4. Phase 0 已实现详情

### 4.1 构建系统
- **生成器**: MinGW Makefiles
- **C++ 标准**: C++20 (required, no extensions)
- **依赖**: 全部通过 `FetchContent` 自动拉取，无需系统安装
- **测试框架**: CTest (1 个 smoke test)

### 4.2 第三方库
| 库 | 版本 | 类型 | 用途 |
|---|------|------|------|
| nlohmann/json | 3.11.3 | header-only | JSON 序列化/反序列化 |
| CLI11 | 2.4.2 | header-only | 命令行参数解析 |
| spdlog | 1.14.1 | header-only | 日志 (Debug/Release) |

> 注: Phase 2 的 HTTP 库将使用 `cpp-httplib` (header-only, WinHTTP, 无外部依赖)，替代原计划的 libcurl。

### 4.3 配置管理 (`src/config/`)
```
AppConfig
├── default_provider: string (默认 "deepseek")
├── providers: map<string, ProviderConfig>
│   ├── deepseek
│   ├── kimi
│   └── claude
├── load() / loadFromFile()  — 从 ~/.novelagent/config.json 加载
├── save()                   — 保存到磁盘
└── setApiKey()              — 环境变量覆盖

ProviderConfig
├── name, api_key, base_url, model
├── context_window (默认 65536)
├── temperature (默认 0.7)
└── max_tokens (默认 4096)
```

**API Key 优先级**: 环境变量 > config.json > 内置默认

### 4.4 CLI 入口 (`src/main.cpp`)
```
novelagent [OPTIONS]

Options:
  -p, --project TEXT    项目目录路径
  -e, --exec TEXT       单次命令模式 (Phase 3 实现)
  --provider TEXT       LLM provider (deepseek/kimi/claude)
  -v, --verbose         调试日志
  -h, --help            显示帮助
```

### 4.5 工具库 (`src/utils/`)
- **FileUtils**: readText, writeText, exists, isDir, createDir/s, listDir, removeDir/File, joinPath, dirName, baseName, homeDir, configDir
- **StringUtils**: ltrim, rtrim, trim, trimmed, split, join, startsWith, endsWith, toLower, toUpper
- **JsonUtils**: getOpt (返回 optional), getOrDefault (带默认值)

---

## 5. 待实现: Phase 1-5 关键设计

### Phase 1: 数据模型 + 项目 I/O
将实现以下数据结构 (全部支持 JSON 序列化):

```
Project
  ├── title, author, description, genre[]
  ├── target_word_count, current_word_count
  ├── pov, tense, status
  └── created, modified

Outline
  ├── premise
  ├── plot_threads[]
  └── chapters[]
       ├── id, title, order, synopsis
       ├── scenes[], pov_characters[]
       ├── key_events[], themes[]
       └── status, word_count

Character
  ├── id, name, role, age
  ├── appearance, personality, background
  ├── traits[], relationships{}
  ├── chapter_appearances[]
  └── arc, notes

Setting
  ├── id, name, category
  ├── description, attributes{}
  └── notes
```

项目目录结构:
```
my-novel/
  novel.json            # Project 元数据
  outline.json          # Outline 层级
  characters.json       # Character[]
  settings.json         # Setting[]
  style.json            # 写作风格配置
  chapters/
    001-chapter.md      # Markdown 格式
    ...
  .novelagent/
    conversation.json   # 对话历史
    summaries.json      # 章节摘要
    state.json          # Agent 状态
```

### Phase 2: LLM 客户端
- `LLMClient` — 单一类，通过 `ProviderConfig` 参数化
- 所有 provider 使用 OpenAI 兼容 `/v1/chat/completions` 端点
- SSE 流式解析 (Server-Sent Events)
- Token 估算: 英文 chars * 0.25, 中文 chars * 0.75

### Phase 3: Agent 核心
```
用户输入 → 追加对话历史
         → ContextManager 组装上下文
         → LLMClient 流式请求
         → 如果有 tool_calls → 执行 → 结果回传 → 循环 (最多10次)
         → 如果没有 → 显示回复 → 追加历史
```

Agent 工具列表 (18 个):
- 章节: read_chapter, write_chapter, create_chapter, append_to_chapter, list_chapters
- 大纲: get_outline, update_outline
- 角色: get_character, create_character, update_character, get_characters
- 设定: get_settings, get_setting, update_setting
- 项目: get_project_status, search_novel, count_words, update_style_config

### Phase 4: 上下文管理
- **预算分配**: 系统提示词(固定) + 输出预留(20%) + 可变预算
  - 50% 当前章节全文 + 大纲 + 角色信息
  - 30% 最近对话
  - 20% 历史压缩摘要
- **降级策略**: 截断章节 → 移除角色详情 → 移除相邻大纲 → 截断对话 → 全压缩
- **为什么不用 RAG**: 小说数据是结构化的，确定性 key 索引比语义检索更精准

### Phase 5: 打磨
- ANSI 颜色输出 (绿=助手, 蓝=用户, 灰=工具)
- 斜杠命令: /help, /save, /load, /clear, /model, /status, /config, /export
- 自动补全、错误恢复、Markdown 导出

---

## 6. 构建与运行

### 前置条件
- CMake >= 3.20
- MinGW64 (g++ 13.1+)
- Git (FetchContent 需要)

### 构建
```bash
mkdir build && cd build
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .
```

### 运行
```bash
# 需要 MinGW bin 在 PATH 中 (libstdc++-6.dll 等)
export PATH="/path/to/mingw64/bin:$PATH"
./build/novelagent.exe --help
```

### 配置 API Key
```bash
export DEEPSEEK_API_KEY="sk-xxxxx"
export KIMI_API_KEY="sk-xxxxx"
export CLAUDE_API_KEY="sk-ant-xxxxx"
```

---

## 7. 关键设计决策

| 决策 | 理由 |
|------|------|
| Header-only 库优先 | 避免系统依赖，FetchContent 一键拉取 |
| 单一 LLMClient 类 | 三家 API 都兼容 OpenAI 格式，无需继承 |
| 项目即目录 | 方便 git 管理，支持手动编辑 |
| 结构化检索 vs RAG | 小说数据按章节/角色/设定索引，确定性更高 |
| cpp-httplib vs libcurl | header-only, WinHTTP 无外部依赖 |
| 自实现 REPL vs replxx | 避免 MinGW 兼容问题，后续可升级 |
| JSON vs YAML/TOML | nlohmann/json 是最成熟的 C++ JSON 库 |

---

## 8. 依赖阶段图

```
Phase 0 (骨架) ✓
    │
    ├── Phase 1 (数据模型) ──┐
    │                        ├── Phase 3 (Agent + REPL) ── Phase 4 (上下文) ── Phase 5 (打磨)
    └── Phase 2 (LLM 客户端) ─┘
         (Phase 1 和 2 可并行)
```
