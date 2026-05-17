# Changelog

## Phase 1: 数据模型 + 项目 I/O (2026-05-17)

### 目标
定义核心数据模型，实现项目文件的读写和项目生命周期管理。

### Step 1.1: 完善 Models.h — 全部数据结构
- `Chapter` struct — id, title, order, synopsis, scenes[], pov_characters[], key_events[], themes[], status, word_count, file_path
- `Character` struct — id, name, role, appearance, personality, background, traits[], relationships{}, chapter_appearances[], arc
- `Setting` struct — id, name, category, description, attributes{}, notes
- `PlotThread` struct — id, name, description
- `Outline` struct — premise, plot_threads[], chapters[]
- `Style` struct — tone, pacing, pov, tense, prose_style, dialogue_style, narrative_distance, chapter_length_target, sentence_length, vocabulary, notes
- 扩展 `Project` struct — format_version, author, genre[], target/current_word_count, status, pov, tense, created, modified, outline, characters, settings, style
- 所有 struct 使用 `NLOHMANN_DEFINE_TYPE_INTRUSIVE` 或手动 `to_json/from_json`
- Project 的 `path` 字段为运行时属性，不参与 JSON 序列化

### Step 1.2: 编写 Models 测试
- `tests/test_models.cpp` — 9 个测试，覆盖所有 6 个 struct 的序列化往返
- 测试空 vector/map 默认值、嵌套结构、middle-language 内容

### Step 1.3: 实现 ProjectIO
- `src/project/ProjectIO.h/.cpp`
- `createProjectDir(path, title)` — 创建完整目录骨架（7 个 JSON + 2 个子目录），幂等操作
- `load(path)` — 从目录加载所有 JSON → Project
- `save(project)` — 保存所有数据到磁盘，自动更新 modified 时间戳
- `readChapter/writeChapter` — Markdown 章节文件读写
- `loadConversation/appendConversation/saveConversation` — 对话历史管理
- `loadJsonFile/saveJsonFile` — 通用 JSON 读写（损坏文件返回 nullopt，不抛异常）

### Step 1.4: 实现 ProjectManager
- `create(path, title)` / `open(path)` / `openOrCreate(path[, title])`
- `isValid(path)` — 检查目录 + novel.json 存在
- `listProjects(baseDir)` — 列出有效项目
- `getDefaultProjectDir(title)` — 标题 → 安全目录名

### Step 1.5: 更新 CMakeLists.txt
- 将 `ProjectIO.h/.cpp` 加入 SOURCES
- 注释更新为中文

### Step 1.6: 编写 ProjectIO 测试
- `tests/test_project_io.cpp` — 7 个测试
- 目录结构完整性、幂等性、save/load 往返、Markdown 读写、对话历史、异常处理

### Step 1.7: 编写 ProjectManager 测试
- 追加 8 个测试到 test_project_io.cpp
- create/open/openOrCreate/isValid/listProjects/getDefaultProjectDir

### Step 1.8: 集成到 main.cpp
- 启动时用 ProjectManager 打开/创建项目
- 显示项目摘要（标题、状态、字数、章节数、角色数）
- `-e` 单次命令模式骨架

### 测试统计
| 可执行文件 | 测试点 | 状态 |
|-----------|--------|------|
| test_main | 1 | 通过 |
| test_models | 9 | 通过 |
| test_project_io | 15 | 通过 |
| **合计** | **25** | **100%** |

---

## Phase 0: 项目骨架 (2026-05-17)

### 目标
搭建构建系统、依赖管理、最小可编译入口。

### 完成事项

**构建系统**
- `CMakeLists.txt` — C++20 项目配置，显式列出源文件
- `cmake/CompilerSettings.cmake` — MSVC/GCC 编译选项，Debug/Release 配置
- `cmake/FetchDependencies.cmake` — FetchContent 自动拉取所有第三方库

**依赖集成**（全部 header-only，无需系统安装）
- `nlohmann/json` v3.11.3 — JSON 序列化
- `CLI11` v2.4.2 — 命令行参数解析
- `spdlog` v1.14.1 — 日志

**入口程序** (`src/main.cpp`)
- 两种运行模式：`-e` 单次命令 / 交互式 REPL（Phase 3 实现）
- CLI 参数：`-p/--project`、`-e/--exec`、`--provider`、`-v/--verbose`
- 环境变量注入 API Key（`DEEPSEEK_API_KEY` 等）

**配置管理** (`src/config/AppConfig.*`)
- `ProviderConfig` 结构体 — 统一描述 LLM provider（name, api_key, base_url, model）
- `AppConfig` — 多 provider 管理，JSON 持久化到 `~/.novelagent/config.json`
- 环境变量覆盖文件配置（`*_API_KEY`）

**工具库** (`src/utils/`)
- `FileUtils.*` — std::filesystem 封装（读写、路径操作、目录管理）
- `StringUtils.h` — trim、split、join、startsWith/endsWith、大小写转换
- `JsonUtils.h` — nlohmann::json 安全取值辅助（getOpt, getOrDefault）

**Stub 文件**（为后续 Phase 预留接口）
- `src/project/Models.h` — Project 数据模型
- `src/project/ProjectManager.*` — 项目打开/创建
- `src/cli/ReplHandler.*` — REPL 循环

**测试与工程**
- `tests/test_main.cpp` — nlohmann/json smoke test（通过）
- `.gitignore` — 排除 build/、配置、示例数据
- 编译验证：MinGW64 g++ 13.1.0，CMake 4.3.0，构建通过

### 未做 / 留待后续
- 未集成 libcurl — Phase 2 改用 cpp-httplib (header-only, WinHTTP)
- REPL 循环、Agent 核心逻辑、LLM 调用均为 stub

### 构建方式
```bash
mkdir build && cd build
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .
```
需要 MinGW bin 目录在 PATH 中才能运行（`libstdc++-6.dll` 等运行时库）。

