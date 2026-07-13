# NovelAgent Qt Widgets 集成 — 最小可行方案

> 本文件记录了 NovelAgent 从 CLI 终端模式向 Qt6 Widgets GUI 的迁移方案。
> 基于 Phase 完成后的讨论确定。

---

## Context

现有 NovelAgent 是基于 C++20 的 CLI 写作工具，通过 `ReplHandler` / `TerminalGUI` 提供终端交互。用户决定放弃 Tauri Web 前端，转向 Qt6 Widgets 作为 GUI 方案。

#### Qt 版本要求

### Qt 版本与路径

- **当前版本：Qt 6.8.3**（安装路径 `D:/QT/QT/6.8.3/mingw_64/`）
- CMake 配置需添加：`set(CMAKE_PREFIX_PATH "D:/QT/QT/6.8.3/mingw_64" ${CMAKE_PREFIX_PATH})`
- 协议：LGPLv3，动态链接，闭源商用无限制

## 核心约束

- **核心层（`src/agent/`、`src/llm/`、`src/project/`、`src/retrieval/`、所有工具）零改动**
- cpp-httplib 保留，不迁移 Qt Network
- CLI 模式保留（通过 `--cli` 参数进入）
- Qt UI 全部放在 `src/novelagent_qt/`，不污染其他目录
- 不引入 QML（先 Widgets + QSS 主题，后续可选局部嵌入）

---

## 设计方案

### 文件结构

```
src/novelagent_qt/
  MainWindow.ui           ← Qt Designer 主窗口布局（菜单栏、splitter、状态栏）
  AgentPanel.ui           ← Qt Designer 聊天面板布局（对话区 + 输入框）
  ProjectTree.ui          ← Qt Designer 项目树布局
  MainWindow.h/.cpp       ← 主窗口信号槽连接 + 业务逻辑
  AgentPanel.h/.cpp       ← 聊天面板业务逻辑
  ProjectTree.h/.cpp      ← 项目树业务逻辑
  ChapterEditor.h/.cpp    ← 纯代码（一个 QPlainTextEdit，简单无需 .ui）
```

### .ui 与纯代码的分工原则

| 场景 | 方式 | 原因 |
|:----|:----|:-----|
| 主窗口骨架（splitter） | `.ui` | Designer 拖拽直接看到效果 |
| 聊天面板布局 | `.ui` | 同上 |
| 项目树布局 | `.ui` | 同上 |
| 复杂表单（角色/设定编辑） | `.ui` | 字段多，拖拽高效，Designer 预览准确 |
| 设置对话框 | `.ui` | 同上 |
| **章节编辑器** | **纯代码** | 一个 QPlainTextEdit + 字数标签，不值得建 .ui |

### 纯代码 UI 规范（必须遵守）

纯代码手写的 UI 组件必须按以下规范书写，保证可读性和一致性：

1. **布局代码集中写在构造函数的 `setupUI()` 私有方法中**，不散落在构造函数各处
2. **变量命名加 `ui_` 前缀**（如 `ui_editor`、`ui_wordCountLabel`），与 `.ui` 编译生成的 `ui_` 前缀风格统
3. **嵌套布局用缩进体现层级**，同一缩进内按 创建→配置→添加 顺序排列

```cpp
// ✅ 正确的纯代码 UI 写法示例
class ChapterEditor : public QWidget {
    Q_OBJECT
public:
    explicit ChapterEditor(QWidget* parent = nullptr);

private:
    void setupUI();

    QPlainTextEdit* ui_editor = nullptr;
    QLabel* ui_wordCountLabel = nullptr;
    agent::Agent* agent_ = nullptr;
    std::shared_ptr<Project> project_;
};

void ChapterEditor::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // 正文编辑器
    ui_editor = new QPlainTextEdit(this);
    ui_editor->setFont(QFont("Microsoft YaHei", 12));
    ui_editor->setPlaceholderText("在此输入章节正文...");
    mainLayout->addWidget(ui_editor);           // 控件占据主要空间

    // 底部状态栏
    auto* bottomBar = new QHBoxLayout;
    ui_wordCountLabel = new QLabel("字数: 0", this);
    bottomBar->addStretch();
    bottomBar->addWidget(ui_wordCountLabel);
    mainLayout->addLayout(bottomBar);            // 状态栏在底部
}
```

4. **禁止在 `setupUI()` 以外的任何地方创建或配置控件**
5. **每个控件的用途添加简短行注释**（一两行即可）
6. **布局嵌套不超过 3 层**，超过 3 层说明应该拆成子 widget 或用 `.ui` 文件

### 其他修改文件

| 文件 | 改动 |
|:----|:-----|
| `src/main.cpp` | 加 Qt 入口分支：`--cli` 走现有 REPL，无参数则启动 `QApplication` + `MainWindow` |
| `CMakeLists.txt` | `find_package(Qt6 REQUIRED COMPONENTS Core Widgets)` + `target_link_libraries` + `qt_add_ui_files` |
| `cmake/Sources.cmake` | 新增 `NOVELAGENT_QT` 和 `NOVELAGENT_QT_UI` 变量 |
| `CLAUDE.md` | 补充 Qt 相关规则（架构隔离、编码转换、线程安全、UI 规范） |
| `docs/DEV_GUIDE.md` | 新增 Qt 集成章节 |

---

### 核心设计

#### 1. 窗口布局（MainWindow.ui）

```
┌─────────────────────────────────────────────────────────┐
│ 菜单栏: 项目 编辑 工具 帮助                              │
├────────┬───────────────────────┬────────────────────────┤
│        │                       │                        │
│ 左侧    │  中央 QTabWidget      │  右侧详情面板          │
│ 项目树  │  ├─ 正文编辑器        │  （角色/设定详情，      │
│ 章节    │  ├─ 大纲视图          │   起步阶段占位）        │
│ 角色    │  └─ 卷/剧情线视图     │                        │
│ 设定    │                       │                        │
│ 规则    ├───────────────────────┤                        │
│        │  底部聊天面板          │                        │
│        │  QTextEdit 对话气泡    │                        │
│        │  QLineEdit 输入框      │                        │
├────────┴───────────────────────┴────────────────────────┤
│ 状态栏: 模型名 | Token 用量 | 上下文 % | Provider       │
└─────────────────────────────────────────────────────────┘
```

所有分区用 `QSplitter` 实现，用户可拖拽调整大小。

#### 2. 流式回调到 UI

```
Agent（工作线程）→ Qt 信号槽（主线程）

AgentPanel.h:
  class AgentPanel : public QWidget {
      Q_OBJECT
  signals:
      void tokenReceived(const QString& token);
      void reasoningReceived(const QString& token);
      void responseComplete(const QString& fullText, int tokens);
      void errorOccurred(const QString& error);
  };

接线处（MainWindow 或调用侧）:
  StreamCallbacks cb;
  cb.on_content = [this](const std::string& delta) {
      emit agentPanel->tokenReceived(
          QString::fromUtf8(delta.c_str(), delta.size()));
  };
  cb.on_reasoning = [this](const std::string& delta) {
      emit agentPanel->reasoningReceived(
          QString::fromUtf8(delta.c_str(), delta.size()));
  };
```

#### 3. 主窗口初始化流程

```
main() 
  → AppConfig::load()
  → 解析 CLI 参数
  → --cli 参数? → novelAgent.runRepl()    ← 走现有路径，不改
  → 否则:
      → QApplication app(argc, argv)
      → NovelAgentApp app(provider, project)  ← 同一装配器
      → MainWindow mainWin(app.agent(), app.project())
      → mainWin.show()
      → app.exec()
```

**不绕过 NovelAgentApp：** Qt 模式依然走它的组件装配（`setupAgent`、`registerAllTools`、`ContextManager` 初始化等），
只是 UI 层从 `ReplHandler` 换成 `MainWindow`。

---

### 各组件实现要点

#### MainWindow（~150 行 + .ui）

- QMainWindow 子类
- 菜单栏：新建/打开项目（复用 ProjectManager）、退出
- 状态栏：模型名、Token 用量、上下文百分比
- 持有 AgentPanel、ProjectTree、ChapterEditor 的指针
- `setupConnections()` 方法连接信号槽

#### AgentPanel（~200 行 + .ui）

- 通过 `setupUi(this)` 加载布局
- `QTextEdit`（只读模式，显示对话历史，带颜色区分角色）
- `QLineEdit`（输入框，Enter 发送 → 调用 `agent_.processUserMessage`）
- `setAgent()` 方法注入 Agent 引用
- 流式 token 逐字符追加到 QTextEdit
- 支持 / 命令（复用 CommandParser 逻辑或直接在 panel 内解析）

#### ProjectTree（~150 行 + .ui）

- 通过 `setupUi(this)` 加载布局
- 顶层节点：章节 / 角色 / 设定 / 世界规则
- 章节子节点：当前大纲中的所有章节（排序显示）
- 角色/设定/规则子节点：对应列表
- 点击章节 → 加载对应 Markdown 到 ChapterEditor
- `refresh()` 方法在项目打开/切换时调用

#### ChapterEditor（~80 行，纯代码）

- 按纯代码 UI 规范书写（见上文）
- QPlainTextEdit + 字数标签
- `loadContent(const std::string& content)` / `saveContent()`
- 同步保存触发 ProjectIO::writeChapter() + Project::markDirty

---

### 依赖与 CMake

```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

set(NOVELAGENT_QT_UI
    src/novelagent_qt/MainWindow.ui
    src/novelagent_qt/AgentPanel.ui
    src/novelagent_qt/ProjectTree.ui
)

set(NOVELAGENT_QT
    src/novelagent_qt/MainWindow.cpp
    src/novelagent_qt/AgentPanel.cpp
    src/novelagent_qt/ProjectTree.cpp
    src/novelagent_qt/ChapterEditor.cpp
)

# novelagent_qt 作为另一个 OBJECT 库
add_library(novelagent_qt OBJECT ${NOVELAGENT_QT})
target_include_directories(novelagent_qt PUBLIC src)
target_link_libraries(novelagent_qt PUBLIC novelagent_core Qt6::Core Qt6::Widgets)

# 处理 .ui 文件
qt_add_ui_files(novelagent_qt ${NOVELAGENT_QT_UI})

# 主 exe 链接它
target_link_libraries(novelagent PRIVATE 
    novelagent_core novelagent_tools novelagent_app novelagent_qt)
```

不需要 `qt_standard_project_setup` 或 `qt_add_executable`，普通 CMake 即可。

---

### 不做的事（边界明确）

- ❌ 不迁移 Qt Network（保持 cpp-httplib）
- ❌ 不引入 QML（起步阶段纯 Widgets）
- ❌ 不改动核心层任何文件
- ❌ 不删除现有 CLI 层
- ❌ 不改动任何工具自注册宏
- ❌ 不加数据库 / 富文本 / WebEngine

---

## 验证方法

1. **编译**：`cmake -B build && cmake --build build`，确认无编译错误
2. **CLI 模式**：`novelagent --cli -p <project>` 进入 REPL，确认原有功能正常
3. **Qt 模式无项目**：`novelagent` 启动空主窗口，项目树显示空提示
4. **Qt 模式有项目**：`novelagent -p <project>` 打开项目，项目树正确显示章节/角色/设定
5. **Agent 对话**：输入消息，确认流式输出逐 token 显示在聊天区
6. **章节编辑**：点击章节 → 内容加载到编辑器 → 修改后保存

全量编译 + 24 个现有测试通过。
