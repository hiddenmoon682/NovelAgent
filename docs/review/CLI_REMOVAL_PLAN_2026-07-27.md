# CLI 代码彻底删除方案

> 生成日期：2026-07-27
> 状态：**📋 待定 — 方案已设计，尚未执行**
> 对应讨论：`SIGINT_HANDLING_REVIEW_2026-07-27.md`
> 关联文件：`src/Bootstrap.h`, `src/NovelAgentApp.h/.cpp`, `src/main_cli.cpp`, `src/main_gui.cpp`, 整个 `src/cli/` 目录

---

## 一、删除目标

彻底移除整个 CLI 层，包含：

### 删除的文件（12 个）

| # | 文件 | 类型 | 理由 |
|---|------|------|------|
| 1 | `src/main_cli.cpp` | 入口点 | CLI 专用入口，QML 前端已有 `main_gui.cpp` |
| 2 | `src/cli/AnsiTerminal.h` | 工具 | ANSI 转义码辅助，QML 不需要终端颜色 |
| 3 | `src/cli/IOutputChannel.h` | 抽象接口 | QML 桥接器有自己的输出机制（Qt 信号） |
| 4 | `src/cli/ConsoleOutput.h` | 实现 | IOutputChannel 的 std::cout 实现，不再需要 |
| 5 | `src/cli/CommandParser.h` | 实现 | REPL 命令解析器，不再需要 |
| 6 | `src/cli/CommandParser.cpp` | 实现 | 同上 |
| 7 | `src/cli/ReplHandler.h` | 实现 | REPL 主循环，不再需要 |
| 8 | `src/cli/ReplHandler.cpp` | 实现 | 同上 |
| 9 | `src/cli/StreamDisplay.h` | 实现 | ANSI 流式输出回调，QML 桥接器自行构造回调 |
| 10 | `src/cli/StreamDisplay.cpp` | 实现 | 同上 |
| 11 | `src/cli/TerminalGUI.h` | 实现 | 终端 GUI 渲染，不再需要 |
| 12 | `src/cli/TerminalGUI.cpp` | 实现 | 同上 |

### 需要改造的文件（5 个）

| # | 文件 | 改动类型 |
|---|------|----------|
| 13 | `src/Bootstrap.h` | 移除 `AnsiTerminal.h` 依赖，改用纯文本 stderr 输出 |
| 14 | `src/NovelAgentApp.h` | 移除 `IOutputChannel.h` 依赖和 `IOutputChannel*` 构造函数参数；移除 `runRepl()`/`runExec()` 声明 |
| 15 | `src/NovelAgentApp.cpp` | 移除 CLI include 和实现代码 |
| 16 | `src/main_gui.cpp` | 移除 `--cli` 回退到 CLI REPL 的逻辑 |
| 17 | `cmake/Sources.cmake` | 移除 `NOVELAGENT_CLI` 列表 |

### 构建系统改动

| # | 文件 | 改动 |
|---|------|------|
| 18 | `CMakeLists.txt` | 移除 `BUILD_CLI` option 和整个 `if(BUILD_CLI)` 分支；移除 CLI 相关 DLL 复制 |

---

## 二、具体改动步骤

### 步骤 1：删除 `src/cli/` 目录下全部 11 个文件

直接从文件系统中删除，无需额外处理。

### 步骤 2：改造 `Bootstrap.h`

**当前代码**（依赖 AnsiTerminal）：
```cpp
#include "cli/AnsiTerminal.h"

// ... 在 run() 中多处使用 Ansi::error(), Ansi::warning(), Ansi::dim(), Ansi::reset()
```

**改为**：使用纯文本 `std::cerr` 输出，去掉 ANSI 颜色代码。

关键替换点（共 5 处）：
- `<< Ansi::error() << "错误: ..." << Ansi::reset()` → `<< "[错误] ..."`
- `<< Ansi::warning() << "警告: ..." << Ansi::reset()` → `<< "[警告] ..."`
- `<< Ansi::dim() << "..." << Ansi::reset()` → `<< "..."`（去掉 dim 修饰）
- `Ansi::enableWindowsAnsi()` 调用移除
- 移除整个 `#include "cli/AnsiTerminal.h"`
- 可以保留 `<CLI/CLI.hpp>`（不依赖 CLI 层，是第三方库，用于命令行参数解析）

### 步骤 3：改造 `NovelAgentApp.h`

**当前代码**：
```cpp
#include "cli/IOutputChannel.h"

class NovelAgentApp {
public:
    NovelAgentApp(const ProviderConfig& provider, std::shared_ptr<Project> project,
                  IOutputChannel* out = nullptr,
                  std::vector<std::string> disabledTools = {});
    void runRepl(const std::string& welcomeMessage = "");
    void runExec(const std::string& command);
private:
    std::unique_ptr<IOutputChannel> ownedOutput_;
    IOutputChannel& out_;
};
```

**改为**：
```cpp
class NovelAgentApp {
public:
    NovelAgentApp(const ProviderConfig& provider, std::shared_ptr<Project> project,
                  std::vector<std::string> disabledTools = {});
    // runRepl() 和 runExec() 移除，QML 侧通过 QmlBridge 交互
    // IOutputChannel 成员完全移除
private:
    // ownedOutput_ 和 out_ 移除
};
```

### 步骤 4：改造 `NovelAgentApp.cpp`

移除以下 include：
```cpp
#include "cli/ConsoleOutput.h"   // 移除
#include "cli/ReplHandler.h"     // 移除
#include "cli/StreamDisplay.h"   // 移除
```

**构造函数**：
```cpp
// 当前：接受 IOutputChannel*，创建 ConsoleOutput 作为默认
NovelAgentApp::NovelAgentApp(const ProviderConfig& provider,
                               std::shared_ptr<Project> project,
                               IOutputChannel* out,
                               std::vector<std::string> disabledTools)
    : ownedOutput_(out ? nullptr : std::make_unique<ConsoleOutput>())
    , out_(out ? *out : *ownedOutput_)
    // ...

// 改为：
NovelAgentApp::NovelAgentApp(const ProviderConfig& provider,
                               std::shared_ptr<Project> project,
                               std::vector<std::string> disabledTools)
    // ownedOutput_ 和 out_ 初始化行移除
    // ...
```

**`runRepl()` 方法**：整个删除
**`runExec()` 方法**：整个删除
（QML 桥接器通过 `QmlBridge::runAgent()` 调用 `agent_.process()`，不走 `NovelAgentApp` 的方法）

### 步骤 5：改造 `main_gui.cpp`

```cpp
// 当前：
int main(int argc, char** argv) {
    auto ctx = bootstrap::run(argc, argv);
    if (ctx.exitCode >= 0) return ctx.exitCode;

    if (!ctx.execCommand.empty()) {
        ctx.app->runExec(ctx.execCommand);   // 移除
        return 0;
    }

    if (ctx.cliMode) {
        ctx.app->runRepl();                  // 移除
        return 0;
    }

    return qtui::runQmlApp(argc, argv, *ctx.app);
}

// 改为：
int main(int argc, char** argv) {
    auto ctx = bootstrap::run(argc, argv);
    if (ctx.exitCode >= 0) return ctx.exitCode;

    return qtui::runQmlApp(argc, argv, *ctx.app);
}
```

同时 `Bootstrap.h` 中的 `Context` 结构体可以精简：
```cpp
struct Context {
    std::unique_ptr<NovelAgentApp> app;
    // execCommand 移除（不再支持 -e）
    // cliMode 移除（不再需要）
    int exitCode = -1;
};
```

`Bootstrap.h` 中的 `run()` 函数也相应精简：移除 `-e` 和 `--cli` 参数注册。

### 步骤 6：改造构建系统

**`cmake/Sources.cmake`**：
```cmake
# 移除整个 NOVELAGENT_CLI 列表定义
```

**`CMakeLists.txt`**：
```cmake
# 移除：
option(BUILD_CLI "Build CLI executable (no Qt dependency)" ON)
# 移除整个 if(BUILD_CLI) ... endif() 块
# 移除 CLI 相关的 DLL 复制
# BUILD_GUI 变为默认唯一构建目标
```

### 步骤 7：更新 `CHANGELOG.md`

在顶部添加一条变更记录。

---

## 三、清理后的文件清单

### 保留的非 CLI 文件（不受影响）

| 目录 | 文件 |
|------|------|
| `src/` | `Bootstrap.h`, `NovelAgentApp.h/.cpp`, `main_gui.cpp`（修改后） |
| `src/agent/` | 全部（无 CLI 依赖） |
| `src/llm/` | 全部（`StreamingPipeline.h` 注释中提到 StreamDisplay，不影响编译） |
| `src/project/` | 全部 |
| `src/config/` | 全部 |
| `src/retrieval/` | 全部 |
| `src/utils/` | 全部 |
| `src/novelagent_qt/` | 全部（QML 前端不受影响） |
| `tests/` | 全部（测试无 CLI 依赖） |

### `Context` 结构体精简后

```cpp
struct Context {
    std::unique_ptr<NovelAgentApp> app;
    int exitCode = -1;
};
```

从 4 个字段减少到 2 个。`execCommand` 和 `cliMode` 不再需要。

### `run()` 函数精简后

移除的参数注册：
- `-e, --exec`（单次执行模式）
- `--cli`（强制终端模式）

保留的参数注册：
- `-p, --project`
- `--provider`
- `-v, --verbose`

---

## 四、影响评估

### 编译影响
- 移除 11 个 CLI 文件 + 1 个入口点，**编译时间减少**（约 5-10%）
- 不再需要链接与 CLI 相关的库
- `BUILD_GUI` 变成唯一构建路径

### 功能影响

| 功能 | 删除前 | 删除后 | 替代方案 |
|------|--------|--------|----------|
| `novelagent_cli` 可执行文件 | ✅ | ❌ | 不再需要 |
| `novelagent_gui --cli` | 终端 REPL | ❌ | 直接启动 QML |
| `novelagent_gui -e "写一章"` | 单次命令执行 | ❌ | 可通过 QML 交互 |
| `novelagent -p ./mybook` | 指定项目 | ✅ | 保留 |
| `novelagent --provider kimi` | 切换 LLM | ✅ | 保留 |
| `novelagent -v` | 调试日志 | ✅ | 保留 |
| 启动前错误提示颜色 | 红色/黄色 ANSI | ❌ | 纯文本 stderr |

### 测试影响
- 无影响（测试中均未引用 CLI 文件）

### QML 前端影响
- 无影响（`QmlBridge` 自行构造 `llm::StreamCallbacks`，不依赖 CLI）
- `NovelAgentApp` 构造函数签名变化需要相应调整 `qtui::runQmlApp()` 的调用

### 第三方依赖影响
- CLI11 仍需保留（用于命令行参数解析）
- spdlog 仍需保留
- nlohmann/json 仍需保留
- **cpr（C++ Requests）可考虑移除**（如果仅被 CLI 层使用）→ 需验证

---

## 五、注意事项

1. **`Bootstrap.h` 的 `Ansi::enableWindowsAnsi()` 调用移除后**，Windows 控制台仍然能正常输出，只是没有颜色。如果有需要，可以在 `main_gui.cpp` 中保留该调用（通过 `#include <windows.h>` 直接调用 `SetConsoleMode()`）。

2. **`execCommand` 支持移除后**，失去脚本化调用能力。如果未来需要，可以通过 QML 的命令行参数启动时自动发送消息来实现（`QmlBridge` 层面处理）。

3. **`NovelAgentApp` 构造函数签名变化**会影响所有调用方，包括：
   - `Bootstrap.h` 中构造 `NovelAgentApp` 的地方
   - 测试中构造 `NovelAgentApp` 的地方
   - `qtui::runQmlApp()` 内部（如果它构造 `NovelAgentApp`）
   
   需要同步更新所有调用点。

4. **`StreamDisplay` 删除后**，如果未来需要在终端中测试流式输出，可以在测试中直接构造 `llm::StreamCallbacks` 而无须 `StreamDisplay` 包装。

---

## 六、执行顺序

如果决定执行，建议按以下顺序操作：

```
1. Bootstrap.h           ← 先改，因为它被两个入口文件共用
2. NovelAgentApp.h       ← 接口变更
3. NovelAgentApp.cpp     ← 实现变更
4. main_gui.cpp          ← 入口点简化
5. 删除 cli/ 目录下 11 个文件
6. 删除 main_cli.cpp
7. 修改 cmake/Sources.cmake
8. 修改 CMakeLists.txt
9. 验证编译（cmake --build）
10. 运行测试（ctest）
11. 更新 CHANGELOG.md
```

每一步都是增量可回退的。

---

## 七、是否执行的决策

| 因素 | 评价 |
|------|------|
| 代码量减少 | ✅ 约 2000 行 |
| 构建简化 | ✅ 单一目标 |
| 维护负担减轻 | ✅ 少维护 11 个文件 |
| 失去独立调试能力 | ⚠️ 调试必须启动 QML |
| 失去脚本调用能力 | ⚠️ `-e` 不再可用 |
| 启动前错误提示无色 | ⚠️ 纯文本，可接受 |
| 改动风险 | ⚠️ 中等（涉及 Bootstrap/NovelAgentApp 接口变更） |

### 关于"以后可能需要"的说明

> Git 记录了完整的历史。删除的代码不会丢失——`git revert` 或 `git log` 即可找回。
> 留着死代码不删，每天都要编译、阅读、维护，这才是真正的长期成本。

### 结论

**推荐方案 A（彻底删除）**，理由：

1. **Git 是安全网**——CLI 代码永远在 Git 历史中，需要时随时恢复
2. **不要为"可能"付费**——如果 QML 前端已经覆盖了所有交互场景，就没有理由保留两套 UI 层
3. **减少认知负担**——新人看代码时不会困惑"为什么有 CLI 和 GUI 两套"
4. **简化构建**——移除 `BUILD_CLI`/`BUILD_GUI` 二分，一个 CMake 目标走到底

如果将来确实需要 CLI 能力：
- **短期**：`git revert <删除CLI的提交>` 即可一键恢复
- **长期**：可以从 Git 历史中摘取 `StreamDisplay` 和 `ReplHandler` 的核心逻辑，适配到新的输出抽象上——会比 11 个文件一起复活更干净
