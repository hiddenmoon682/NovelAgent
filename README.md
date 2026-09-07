# NovelAgent — AI 小说创作助手

NovelAgent 是一个用 **C++20** 编写的 AI 小说创作助手桌面应用。它对接大语言模型（OpenAI 兼容 API），以「墨染」暖墨深色 Qt Quick 界面承载完整的创作工作流：小说项目管理、多轮创作对话与工具调用、章节撰写与阅读、语义检索与长期记忆。

> 小说正文以纯文本章节（`.txt`）存储，不含 markdown 标记，按卷组织。

---

## 功能特性

### 创作对话（Agent 引擎）

- **多轮对话 + 流式输出**：SSE 流式解析与累积，边生成边显示；
- **工具调用**：章节/角色/大纲/记忆的读写工具，`REGISTER_TOOL` 宏自注册——新增工具零装配，执行管线统一处理校验、截断与错误隔离；
- **上下文分层注入**：全局规则（`~/.novelagent/rules.md`）+ 项目规则（`<项目>/.novelagent/rules.md`）+ 按需加载技能（YAML frontmatter + `use_skill`）；
- **上下文可控**：Token 预算评估 + 自动压缩（Compactor），长对话不爆窗口；重试采用指数退避（429/502/503 与网络错误），工具结果超限自动截断。

### 小说项目管理

- 创建 / 编辑 / 打开小说项目，最近项目列表；
- 章节以纯文本存储，按卷组织（卷 ID / 卷序 / 卷标题）；
- 读写分离接口（`IProjectReader` / `IProjectWriter`），项目文件锁防并发破坏。

### 检索与记忆（RAG）

- **语义分块**：NovelChunker 按纯文本语义起止点切分章节（含重叠），适配长文；
- **向量召回**：章节 / 记忆嵌入（EmbeddingGenerator）→ sqlite-vec 向量库（`novel.db` 的 `vec_chunks`），语义检索结果注入对话上下文；
- **全文索引**：SQLite FTS5（编译期 `SQLITE_ENABLE_FTS5`）；
- **长期记忆**：LongTermMemoryStore 与 `memories` 资产，跨会话留存设定与偏好。

### 桌面 GUI（墨染）

- **三栏布局**：左侧最近项目 / 会话侧栏、中部创作对话（消息气泡 / 工具调用卡片 / 技能弹窗）、右侧章节阅读器；
- **目录抽屉**：搜索即时过滤、卷分组（`ViewSection`）、当前章节自动定位、`↑↓`/`Enter`/`Esc` 键盘导航；
- **主题统一**：全部颜色 / 字号 / 间距 / 圆角取自 `Theme.qml` 单例，深色暖墨风格；
- **设置弹窗**：Provider / 模型 / 主题 / 索引管理；无边框自绘窗口、全局 Toast、模态弹窗统一遮罩（`ModalDimmer`）。

---

## 架构概览

核心组件依赖抽象接口（依赖倒置），由 `NovelAgentApp` 门面统一装配：

| 抽象接口 | 实现 | 职责 |
|----------|------|------|
| `llm::ILLMClient` | `llm::LLMClient` | 多 Provider 对话、SSE 流式、重试 |
| `IProjectReader` / `IProjectWriter` | `ProjectAccess(Project&)` | 项目读写，读写分离 |
| `IVectorStore` | `SqliteVectorStore` | 语义向量召回 |
| `IOutputChannel` | `ConsoleOutput` 等 | 输出通道抽象 |

### 源码模块（`src/`）

| 目录 | 职责 |
|------|------|
| `llm/` | 多 Provider 客户端、SSE 解析、流式累积、Token 统计、Emoji 过滤 |
| `agent/` | 对话循环（CoreLoop）、工具注册与执行管线、上下文（预算评估 / 压缩）、提示词组装、规则提供、项目索引、长期记忆、会话管理 |
| `project/` | 项目数据模型、文件读写（ProjectIO）、读写分离适配 |
| `retrieval/` | 章节分块（NovelChunker）、嵌入生成、向量存储 |
| `storage/` | SQLite 持久化（会话 / 消息 / 向量 / 记忆）、文件存储后端 |
| `config/` `utils/` | 配置加载（Provider / 模型）、通用工具（文件、Schema 构建） |
| `novelagent_qt/` | Qt QML GUI：`QmlBridge` 桥接 C++ 与 QML，QML 不直接访问 C++ |

GUI 数据流全部经由 `QmlBridge`（`Q_INVOKABLE` / property / signal）与后端通信，列表刷新响应后端信号后重建 `ListModel`，避免过期数据残留。

---

## 技术栈

| 类别 | 选型 |
|------|------|
| 语言 / 构建 | C++20（GCC）、CMake + Ninja、MSYS2 MinGW-w64 |
| GUI | Qt 6.8.3（Qt Quick / QuickControls2，QML） |
| HTTP / JSON | cpp-httplib（`third_party` 本地副本）+ OpenSSL、nlohmann/json |
| 日志 | spdlog |
| 数据库 | SQLite 官方 amalgamation + sqlite-vec + SQLiteCpp（均 `third_party` 内置） |
| 其他 | yaml-cpp（SKILL frontmatter）、simdutf（UTF-8/16/32 转码） |

**依赖策略**：优先使用系统包管理器（MSYS2 pacman）安装的版本；未安装时 CMake 自动回退 `FetchContent` 浅克隆源码编译；cpp-httplib 无系统包，始终使用本地副本。

---

## 构建

### 环境要求

- MSYS2 MinGW-w64 工具链（含 Ninja、OpenSSL、yaml-cpp 可选）；
- Qt 6.5+（开发机使用 Qt 6.8.3 mingw_64 套件）；
- OpenSSL（`mingw-w64-x86_64-openssl`）。

### 构建与运行

```bash
# 1. 配置（首次或 CMakeLists 变更后）
cmake --preset default

# 2. 构建
cmake --build --preset default

# 3. 运行
./build/novelagent_gui.exe   # Windows 下构建自动复制 MSYS2 DLL，可双击运行
```

### 构建预设

| 预设 | 输出目录 | 说明 |
|------|----------|------|
| `default` | `build/` | Debug，仅构建项目，不含测试 |
| `test` | `build-test/` | Debug + `BUILD_TESTING=ON`，供 ctest 使用 |
| `release` | `build-release/` | Release（`-O3`），不含测试 |

### 运行配置

`config.json`（已加入 `.gitignore`，**不入库**）配置 API Provider，默认 DeepSeek 示例：

```json
{
  "default_provider": "deepseek",
  "providers": {
    "deepseek": {
      "name": "deepseek",
      "api_key": "在此填入你的 API Key",
      "base_url": "https://api.deepseek.com",
      "model": "deepseek-chat",
      "max_context_tokens": 1000000,
      "temperature": 0.7,
      "max_tokens": 8192,
      "enable_thinking": true
    }
  }
}
```

---

## 测试

```bash
cmake --preset test
cmake --build --preset test
ctest --test-dir build-test --output-on-failure
```

- 单元测试按模块划分（models / LLM 客户端 / 上下文 / 工具管线 / 检索 / 记忆 / 项目 IO 等）；
- LLM 路径使用本地模拟 HTTP Server（httplib）+ SSE 桩数据测试，不依赖真实网络；
- 端到端测试标记为手动执行，不在 CTest 自动运行。

---

## 目录结构（节选）

```
src/                核心源码（llm / agent / project / retrieval / storage / config / utils / novelagent_qt）
third_party/        SQLite 系、simdutf、cpp-httplib 等内置依赖
cmake/              模块源清单、依赖管理、编译器设置
tests/              单元测试（本地开发仓库内容）
docs/               设计 / 开发指南（本地开发仓库内容）
CHANGELOG.md        变更记录（按日期倒序）
scripts/            本地验证脚本（本地开发仓库内容）
```

> 说明：`tests/`、`docs/`、`scripts/` 为本地开发仓库内容，未随公开仓库推送；公开仓库包含 `src/`、`cmake/`、`third_party/` 与 `CMakeLists.txt`。

---

## 变更记录

见 [CHANGELOG.md](CHANGELOG.md)（按日期倒序，最新在最上）。

## 许可

未指定开源许可证，保留所有权利。