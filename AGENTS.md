# AGENTS.md — NovelAgent 开发入口

> 面向开发 Agent（Qoder / Claude Code / Cursor 等）的项目直达信息。
> 这是给"改代码的 Agent"的入口；给"小说创作 LLM"的运行时约束在 `~/.novelagent/rules.md` 与 `<project>/.novelagent/rules.md`，二者不要混淆。

## 项目概述

- **语言/标准**：C++20，CMake 构建（Ninja），MSYS2 MinGW-w64。
- **核心依赖**：`nlohmann/json`、`spdlog`、`cpp-httplib`、OpenSSL、FTXUI。
- **结构**：`src/`（agent/llm/project/retrieval/utils/novelagent_qt）、`tests/`（`test_<module>.cpp`）、`docs/`（设计/规划/审查）、`cmake/`、`scripts/`。
- **详细规范**：完整开发规范见 [CLAUDE.md](CLAUDE.md) 与 [docs/DEV_GUIDE.md](docs/DEV_GUIDE.md)。

## 构建与验证命令

> 统一机械验证门：`./scripts/verify.sh`（MSYS2 bash 下运行）。改动后一律走它，避免手写命令遗漏或失败信息丢失。

```bash
# 配置（首次或 CMakeLists 变更后）
cmake --preset default

# 机械验证门（默认聚焦，按模块名过滤测试）
./scripts/verify.sh <模块名>    # 构建 + 仅跑覆盖该模块的测试（默认做法）
./scripts/verify.sh             # 构建 + 全量回归（临近交付/推送前）
./scripts/verify.sh --build     # 仅构建
./scripts/verify.sh --list      # 列出全部测试名
```

失败诊断与复验：`verify.sh` 用 `--output-on-failure` 保留失败用例输出，据此定位根因；修复后重跑同一命令用 `--output-on-failure` 复验，确认失败消失后再进入评审。

## 决策规则

- **聚焦验证优先**：默认只跑与本次改动直接相关的测试（按改动模块/文件选取覆盖它们的用例），不跑全量 ctest。仅在大范围改动、跨模块影响或临近交付/推送前才做全量回归。
- **注释用中文**：所有注释、docstring、文档说明必须中文；代码标识符用英文；Git 提交信息用中文。
- **变更记录**：增量写入根目录 `CHANGELOG.md`（按日期倒序，最新在上），不另建 changelog 文件。

## 高风险操作边界

- **禁止**：破坏 PCH（`nlohmann/json.hpp` + `spdlog/spdlog.h` + STL）、切回 Makefiles、破坏已配置的编译加速。
- **禁止**：手动编辑 `AgentSetup.cpp` 注册工具（用 `REGISTER_TOOL` 宏）。
- **禁止**：热路径中的 O(n²)、高频 string/json 拷贝、重复文件 I/O、手工拼接 JSON 字符串。
- **依赖倒置**：核心组件依赖抽象接口（`ILLMClient`、`IProjectReader`、`IOutputChannel`、`IVectorStore`），不直接持有具体类引用。