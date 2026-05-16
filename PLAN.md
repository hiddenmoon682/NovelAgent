# NovelAgent CLI -- 实现计划

## 背景

构建一个 AI 辅助写小说的工具。第一阶段是类似 Claude Code 的 CLI 交互式工具，后续扩展到 GUI/Web。技术栈：CLI 阶段用 C++，后续页面用其他语言。LLM 使用 DeepSeek / Kimi 等国内 API。

---

## 1. 项目目录结构

```
D:\C++Code\C++NovelAgent\
  CMakeLists.txt
  cmake/
    FetchDependencies.cmake       # FetchContent 拉取所有依赖
    CompilerSettings.cmake        # 编译选项、警告

  src/
    main.cpp                      # 入口，CLI 参数解析，分发

    cli/
      ReplHandler.h / .cpp        # 交互式 REPL 循环
      StreamDisplay.h / .cpp      # 终端输出：流式 token、颜色
      CommandParser.h / .cpp      # 斜杠命令解析 (/help, /save, /model 等)

    llm/
      Message.h                   # Message, ToolCall, ToolDefinition 数据结构
      LLMClient.h / .cpp          # HTTP + OpenAI 兼容 API 客户端
      SSEParser.h / .cpp          # SSE 流式解析
      TokenCounter.h / .cpp       # Token 估算（启发式算法）

    project/
      Models.h                    # Project, Chapter, Character, Setting, Outline
      ProjectIO.h / .cpp          # 磁盘读写
      ProjectManager.h / .cpp     # 创建、打开、列举项目

    agent/
      Agent.h / .cpp              # 核心 Agent 循环
      ToolRegistry.h / .cpp       # 工具注册、描述、调度
      ContextManager.h / .cpp     # 上下文窗口预算和组装
      tools/
        ChapterTools.cpp          # read/write/create/list chapter
        CharacterTools.cpp        # get/create/update character
        SettingTools.cpp          # get/update setting
        OutlineTools.cpp          # get/update outline
        ProjectTools.cpp          # project_info, search 等

    config/
      AppConfig.h / .cpp          # 全局配置（provider, model, API key）

    utils/
      FileUtils.h / .cpp
      StringUtils.h / .cpp
      JsonUtils.h / .cpp

  tests/
    CMakeLists.txt
    test_project_io.cpp
    test_sse_parser.cpp
    test_tool_registry.cpp
    test_context_manager.cpp
```

---

## 2. 依赖选择

| 库 | 用途 | 集成方式 | 理由 |
|---|------|---------|------|
| **nlohmann/json** | JSON 解析 | FetchContent (header-only) | 事实标准，最易用 |
| **CLI11** | CLI 参数解析 | FetchContent (header-only) | 轻量，子命令支持 |
| **spdlog** | 日志 | FetchContent (header-only) | 高性能，支持文件/控制台 |
| **libcurl** | HTTP 请求 | 系统安装 (`pacman -S mingw-w64-x86_64-curl`) | 最成熟的 HTTP 库，支持 SSL、流式 |

**REPL 方案**：使用 `std::getline` + 简单自定义实现，避免在 MinGW 上引入 replxx 的编译兼容性问题。后续可升级为 replxx。

---

## 3. LLM Provider 抽象

DeepSeek、Kimi、Claude 都提供 OpenAI 兼容的 `/v1/chat/completions` 端点，**不需要 Provider 继承体系**——只需一个 `LLMClient` 类，通过 `ProviderConfig` 参数化：

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

API Key 来源优先级：环境变量 > `~/.novelagent/config.json` > 项目本地配置。

---

## 4. 上下文管理策略（核心难点）

长篇小说的上下文窗口管理策略——**LLM 不需要看到全部内容**：

### 预算分配
- 系统提示词 + 工具定义：~1500 tokens（固定）
- 输出预留：上下文窗口的 20%
- 剩余可变预算：
  - **50%** — 当前章节全文 + 大纲 + 场景角色信息
  - **30%** — 最近对话（原始消息）
  - **20%** — 历史压缩摘要

### 降级策略
当预算不足时按优先级裁剪：
1. 截断当前章节到末尾 2000 字
2. 移除角色详细档案（LLM 改用 `get_character` 工具查询）
3. 移除相邻章节大纲
4. 截断对话到最近 5 轮
5. 全文压缩为摘要

### 摘要持久化
- 章节摘要存储在 `.novelagent/summaries.json`
- 每次修改章节后自动更新摘要
- 对话历史摘要随会话进行逐步更新

**为什么不用 RAG/向量检索？** 小说的数据是结构化的（章节、角色、设定），通过确定性 key 索引比语义检索更精确。MVP 阶段不需要引入向量数据库的复杂度。

---

## 5. 项目文件格式

每个小说的项目是一个**目录**（方便 git 管理、手动编辑）：

```
my-novel/
  novel.json              # 项目元数据（标题、字数、类型等）
  outline.json            # 层级章节大纲
  characters.json         # 角色档案
  settings.json           # 世界观设定
  style.json              # 写作风格配置
  chapters/
    001-introduction.md
    002-the-arrival.md
    ...
  .novelagent/
    conversation.json     # 完整对话历史
    summaries.json        # 章节摘要缓存
    state.json            # Agent 状态（当前章节、当前模型等）
```

章节文件使用 Markdown 格式，可直接在任何编辑器中打开修改。

---

## 6. CLI 系统

两种运行模式：
- **直接命令**：`novelagent -p my-novel -e "写第三章的第一场戏"`
- **交互 REPL**：`novelagent -p my-novel`，进入对话模式

### 斜杠命令（本地处理，不发送给 LLM）

| 命令 | 功能 |
|------|------|
| `/help` | 显示帮助 |
| `/save` | 保存当前项目 |
| `/load <path>` | 切换/打开项目 |
| `/clear` | 清空当前对话（保留项目数据） |
| `/model <provider> <model>` | 切换 LLM |
| `/status` | 显示项目状态（字数、章节数等） |
| `/config <key> <value>` | 修改配置 |
| `/export` | 导出完整小说为 Markdown |

### Agent 工具（由 LLM 通过 function calling 调用）

| 工具名 | 功能 |
|--------|------|
| `read_chapter` | 读取章节内容 |
| `write_chapter` | 写入/覆盖章节 |
| `create_chapter` | 创建新章节 |
| `append_to_chapter` | 追加内容到章节 |
| `list_chapters` | 列出所有章节 |
| `get_outline` / `update_outline` | 大纲操作 |
| `get_character` / `create_character` / `update_character` | 角色操作 |
| `get_settings` / `get_setting` / `update_setting` | 设定操作 |
| `get_project_status` | 项目统计 |
| `search_novel` | 全文搜索 |
| `count_words` | 字数统计 |
| `update_style_config` | 修改写作风格 |

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

## 8. 分阶段实现

### Phase 0：项目骨架（1-2 天）
- CMakeLists.txt + FetchDependencies
- main.cpp stub
- AppConfig 基础版
- 编译通过、测试框架就绪

### Phase 1：数据模型 + 项目 I/O（3-4 天）
- `src/project/` 全部文件
- 所有 JSON 序列化/反序列化
- 项目目录的创建、加载、保存

### Phase 2：LLM 客户端（3-4 天）
- `src/llm/` 全部文件
- HTTP 请求 + SSE 流式解析
- Token 估算
- 使用 Mock HTTP 服务器测试

> Phase 1 和 Phase 2 **可并行开发**（互不依赖）

### Phase 3：Agent + REPL MVP（4-5 天）
- `src/agent/` + `src/cli/` 全部文件
- 完整交互：用户输入 → LLM → 工具调用 → 回复
- 基础上下文管理（仅截断，无摘要）
- `/save`, `/load`, `/help`, `/clear` 命令

### Phase 4：上下文管理（3-4 天）
- 章节摘要自动生成
- 对话历史压缩
- 多级降级策略
- 会话持久化

### Phase 5：打磨（2-3 天）
- 颜色输出、Markdown 渲染
- 斜杠命令自动补全
- 错误恢复、自动保存
- `/export` 导出

---

## 关键技术风险

| 风险 | 缓解措施 |
|------|---------|
| libcurl 在 MinGW 上链接问题 | 用 MSYS2 安装：`pacman -S mingw-w64-x86_64-curl` |
| 中文 Token 估算不准确 | 启发式（字数 × 1.3），标注为估算值，实际以 API 返回为准 |
| Console 中文显示 | 启用 UTF-8 codepage，使用 ANSI 转义码 |
| API Key 泄露到 git | 将 config 文件加入 `.gitignore`，只用环境变量 |

---

## 验证方法

- Phase 1：单元测试验证 ProjectIO 读写往返一致性
- Phase 2：Mock HTTP Server 测试 SSE 解析和 Token 计数
- Phase 3：连接真实 API，完成"创建角色 → 写大纲 → 写第一章"的端到端流程
- Phase 4：构造超长对话（200+ 轮），验证上下文降级不丢关键信息
- Phase 5：在 Windows Terminal / Git Bash 中验证颜色和中文显示
