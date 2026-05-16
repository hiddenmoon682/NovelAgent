# Changelog

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
