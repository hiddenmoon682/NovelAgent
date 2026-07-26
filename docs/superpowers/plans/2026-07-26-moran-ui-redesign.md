# 「墨染书房」UI 改版实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 Qt/QML GUI 换为「墨染书房」暖墨主题 + 三栏布局，并补齐工具调用展示、思考过程折叠、消息复制、章节列表四项可用性能力。

**Architecture:** C++ 侧最小扩展——`llm::StreamCallbacks` 增加带工具名的 `on_tool_start`/`on_tool_finish`（由 `CoreLoop` 在执行工具前后触发，沿 `QmlBridge → Agent::process → CoreLoop` 既有传递链路，无需改 Agent API）；`QmlBridge` 新增章节读取接口。QML 侧重写主题单例、合并左侧两栏、按消息类型分发气泡/工具卡片。

**Tech Stack:** C++20 / Qt 6.5 (Quick + QuickControls2) / CMake preset `default`（Ninja + MinGW，构建目录 `build/`）

**规范说明：**
- 本项目 spec 位于 `docs/superpowers/specs/2026-07-26-moran-ui-redesign-design.md`。与 spec 3.1 的差异：工具回调放在 `llm::StreamCallbacks`（而非 `CoreLoopHooks`），因 StreamCallbacks 已从 QmlBridge 一路传到 CoreLoop，无需改 Agent 公共 API（spec 已同步更新）。
- **git 提交：用户未要求自动提交，所有 Task 完成后不执行 git commit；若用户明确要求再提交。**
- QML 无自动化测试，涉及 QML 的 Task 以「构建通过 + 启动手动核对」为验收；C++ 回调扩展走 TDD。

**File Structure:**

| 文件 | 操作 | 职责 |
|---|---|---|
| `src/llm/ILLMClient.h` | 修改 | StreamCallbacks 增加 on_tool_start/on_tool_finish |
| `src/agent/core/CoreLoop.cpp` | 修改 | 工具执行前后触发回调 |
| `tests/test_core_loop.cpp` | 修改 | 新增回调测试 |
| `src/novelagent_qt/QmlBridge.h/.cpp` | 修改 | 工具信号带实名、chapterList/loadChapter/chaptersChanged |
| `src/novelagent_qt/qml/Theme.qml` | 重写 | 暖墨配色 token |
| `src/novelagent_qt/qml/SidebarPanel.qml` | 新建 | 合并项目+会话的左侧栏 |
| `src/novelagent_qt/qml/ProjectPanel.qml`、`SessionPanel.qml` | 删除 | 被 SidebarPanel 取代 |
| `src/novelagent_qt/qml/MainWindow.qml` | 修改 | 三栏 SplitView、窗口按钮 restyle |
| `src/novelagent_qt/qml/ToolCallCard.qml` | 新建 | 工具调用状态卡片 |
| `src/novelagent_qt/qml/AgentPanel.qml` | 重写 | 消息模型带 type/reasoning、空状态建议卡、输入提示 |
| `src/novelagent_qt/qml/ChatBubble.qml` | 重写 | reasoning 折叠、复制按钮 |
| `src/novelagent_qt/qml/ReaderPanel.qml` | 重写 | 章节下拉选择、底部字数 |
| `src/novelagent_qt/qml/StatusBar.qml` | 修改 | 上下文进度条、合并 provider·model |
| `CMakeLists.txt` | 修改 | NOVELAGENT_QML_FILES 更新 |

---

### Task 1: StreamCallbacks 工具生命周期回调（C++，TDD）

**Files:**
- Modify: `src/llm/ILLMClient.h:29-35`
- Modify: `src/agent/core/CoreLoop.cpp:198-212`
- Test: `tests/test_core_loop.cpp`

- [x] **Step 1: 写失败测试**

在 `tests/test_core_loop.cpp` 的 `test_reasoning_content_preserved()` 之后（`int main()` 之前）添加：

```cpp
void test_tool_lifecycle_callbacks() {
    TEST("CoreLoop — on_tool_start/on_tool_finish 回调");
    MockSeqLLMClient client;
    // 第 1 轮：返回 1 个 tool_call
    {
        llm::LLMResponse r;
        r.finish_reason = "tool_calls";
        llm::ToolCall tc;
        tc.id = "call_1";
        tc.function_name = "read_chapter";
        tc.arguments = "{}";
        r.tool_calls.push_back(tc);
        client.addResponse(r);
    }
    // 第 2 轮：返回文本
    {
        llm::LLMResponse r;
        r.content = "完成";
        r.finish_reason = "stop";
        client.addResponse(r);
    }

    agent::ToolRegistry registry;
    auto mockTool = std::make_unique<MockReadTool>();
    registry.registerBuiltInTool(std::move(mockTool));

    llm::Memory conv;
    conv.addUser("读一下第一章");

    std::vector<std::string> started;
    std::vector<std::string> finished;
    bool all_ok = true;
    llm::StreamCallbacks cb;
    cb.on_tool_start = [&](const std::string& name) { started.push_back(name); };
    cb.on_tool_finish = [&](const std::string& name, bool ok) {
        finished.push_back(name);
        all_ok = all_ok && ok;
    };

    agent::ToolPipeline pipeline(registry, 0);
    agent::CoreLoop loop(client, registry, pipeline);
    auto result = loop.run(conv, registry.getToolDefinitions(), "", cb, {});
    CHECK(result.rounds_executed == 2);
    CHECK(started.size() == 1);
    CHECK(started[0] == "read_chapter");
    CHECK(finished == started);
    CHECK(all_ok);
    PASS();
}
```

并在 `int main()` 中现有测试调用之后追加一行：

```cpp
    test_tool_lifecycle_callbacks();
```

- [x] **Step 2: 运行确认编译失败**

Run: `cmake --build build --target test_core_loop`
Expected: 编译错误 `'struct llm::StreamCallbacks' has no member named 'on_tool_start'`

- [x] **Step 3: 扩展 StreamCallbacks**

`src/llm/ILLMClient.h`，在 struct 中 `on_tool_call_start` 之后添加两个成员（并在上方注释块的回调列表中补两行说明）：

```cpp
struct StreamCallbacks {
    std::function<void(const std::string& delta)> on_content;
    std::function<void(const std::string& delta)> on_reasoning;
    std::function<void()> on_tool_call_start;
    // 工具执行生命周期（由 CoreLoop 在执行工具前后触发，携带工具名；UI 可据此展示状态卡片）
    std::function<void(const std::string& tool_name)> on_tool_start;
    std::function<void(const std::string& tool_name, bool ok)> on_tool_finish;
    std::function<void(const LLMResponse& response)> on_complete;
    std::function<void(const std::string& error)> on_error;
};
```

- [x] **Step 4: CoreLoop 触发回调**

`src/agent/core/CoreLoop.cpp` `runImpl()` 中，将「正常路径」的工具执行段（原第 201-209 行）改为：

```cpp
        if (state_) state_->transition(AgentState::AwaitingTool);

        for (const auto& tc : response.tool_calls)
            if (callbacks.on_tool_start) callbacks.on_tool_start(tc.function_name);

        try {
            auto diff = pipeline_.execute(response.tool_calls);
            memory.apply(diff);
            for (const auto& tc : response.tool_calls)
                if (callbacks.on_tool_finish) callbacks.on_tool_finish(tc.function_name, true);
        } catch (...) {
            for (const auto& tc : response.tool_calls)
                if (callbacks.on_tool_finish) callbacks.on_tool_finish(tc.function_name, false);
            memory.popBack();
            throw;
        }
```

- [x] **Step 5: 运行测试确认通过**

Run: `cmake --build build --target test_core_loop` 然后 `ctest --test-dir build -R test_core_loop --output-on-failure`
Expected: 全部 PASSED（含新增用例），无回归

---

### Task 2: QmlBridge — 工具信号实名化 + 章节接口

**Files:**
- Modify: `src/novelagent_qt/QmlBridge.h`
- Modify: `src/novelagent_qt/QmlBridge.cpp`

- [x] **Step 1: 头文件声明**

`QmlBridge.h`：
1. `#include <QString>` 之后添加 `#include <QVariantList>`
2. Q_INVOKABLE 区块添加：

```cpp
    // 章节列表（按 order 升序）：[{id, title, order, wordCount}, ...]；项目未打开返回空。
    Q_INVOKABLE QVariantList chapterList() const;
    // 读取章节正文 Markdown；失败返回空串并 emit errorOccurred。
    Q_INVOKABLE QString loadChapter(const QString& chapterId);
```

3. signals 区块中 `void toolCallStarted(const QString& toolName);` 保持不变，其后添加：

```cpp
    void toolCallFinished(const QString& toolName, bool ok);
    // 章节数据可能变化（响应完成 / 手动刷新项目后发射）
    void chaptersChanged();
```

- [x] **Step 2: 实现章节接口**

`QmlBridge.cpp`：顶部 `#include "project/Models/Project.h"` 之后添加 `#include "project/ProjectIO.h"` 与 `#include <algorithm>`。在 `refreshProject()` 实现之后添加：

```cpp
QVariantList QmlBridge::chapterList() const {
    QVariantList list;
    if (!project_) return list;

    std::vector<const Chapter*> sorted;
    sorted.reserve(project_->outline.chapters.size());
    for (const auto& ch : project_->outline.chapters)
        sorted.push_back(&ch);
    std::sort(sorted.begin(), sorted.end(),
              [](const Chapter* a, const Chapter* b) { return a->order < b->order; });

    for (const auto* ch : sorted) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), QString::fromStdString(ch->id));
        m.insert(QStringLiteral("title"), ch->title.empty()
                     ? QStringLiteral("第 %1 章").arg(ch->order)
                     : QString::fromStdString(ch->title));
        m.insert(QStringLiteral("order"), ch->order);
        m.insert(QStringLiteral("wordCount"), ch->word_count);
        list.push_back(m);
    }
    return list;
}

QString QmlBridge::loadChapter(const QString& chapterId) {
    if (!project_) return {};
    const std::string id = chapterId.toStdString();
    for (const auto& ch : project_->outline.chapters) {
        if (ch.id != id) continue;
        if (ch.file_path.empty()) {
            emit errorOccurred(QStringLiteral("章节尚未写入正文文件"));
            return {};
        }
        const std::string content = ProjectIO::readChapter(project_->path, ch.file_path);
        if (content.empty())
            emit errorOccurred(QStringLiteral("章节文件不存在或为空: ")
                               + QString::fromStdString(ch.file_path));
        return QString::fromStdString(content);
    }
    emit errorOccurred(QStringLiteral("未找到章节: ") + chapterId);
    return {};
}
```

- [x] **Step 3: runAgent 接入工具回调**

`runAgent()` 中，将现有 `cb.on_tool_call_start` lambda 改为只更新状态（去掉 `emit toolCallStarted(QString())`）：

```cpp
        cb.on_tool_call_start = [this]() {
            QMetaObject::invokeMethod(this, [this]() {
                setStatus(QStringLiteral("调用工具中..."));
            }, Qt::QueuedConnection);
        };

        cb.on_tool_start = [this](const std::string& name) {
            QString n = QString::fromStdString(name);
            QMetaObject::invokeMethod(this, [this, n]() {
                setStatus(QStringLiteral("调用工具: ") + n);
                emit toolCallStarted(n);
            }, Qt::QueuedConnection);
        };

        cb.on_tool_finish = [this](const std::string& name, bool ok) {
            QString n = QString::fromStdString(name);
            QMetaObject::invokeMethod(this, [this, n, ok]() {
                emit toolCallFinished(n, ok);
            }, Qt::QueuedConnection);
        };
```

- [x] **Step 4: 发射 chaptersChanged**

`runAgent()` 的 responseComplete 转发块内、`emit usageChanged();` 之后添加一行 `emit chaptersChanged();`。`refreshProject()` 改为：

```cpp
void QmlBridge::refreshProject() {
    emit projectChanged();
    emit chaptersChanged();
}
```

- [x] **Step 5: 构建验证**

Run: `cmake --build build --target novelagent_gui`
Expected: 构建成功，无新警告

线程模型说明：`chapterList`/`loadChapter` 在 QML 主线程读取 `project_`，与现有 `projectName()` 相同模式；`chaptersChanged` 仅在主线程（QueuedConnection 回调内）发射，读取时机与 Agent 写入错开，沿用现有设计假设。

---

### Task 3: Theme.qml 换为暖墨配色

**Files:**
- Modify: `src/novelagent_qt/qml/Theme.qml`（整文件替换）

- [x] **Step 1: 替换 Theme.qml 全文**

```qml
pragma Singleton
import QtQuick

// Theme — 全局主题常量（「墨染书房」暖墨主题）。
// 单一事实来源：所有 QML 组件从此处读取颜色/字体/间距。
QtObject {
    // ── 背景层级（按栏位语义化，左深右浅突出对话区）──
    readonly property color bgSidebar:   "#141210"   // 左侧栏（最深）
    readonly property color bgChat:      "#201c17"   // 对话区（最浅，视觉焦点）
    readonly property color bgReader:    "#181511"   // 阅读区
    readonly property color bgElevated:  "#2a251d"   // 输入框/悬浮卡片
    readonly property color bgHover:     "#302a20"   // hover 高亮

    // ── 文字 ──
    readonly property color textPrimary:   "#e8e2d5"   // 宣纸色
    readonly property color textSecondary: "#9a9184"
    readonly property color textFaint:     "#6b6355"

    // ── 强调色 ──
    readonly property color accent:        "#c9553e"   // 朱砂：按钮/选中/用户标识
    readonly property color accentSoft:    "#8c3f2e"   // 用户消息气泡底
    readonly property color agentTint:     "#a3b48a"   // 青竹：Agent 标识/工具卡片
    readonly property color warning:       "#d4a373"   // 琥珀警示
    readonly property color danger:        "#c0392b"

    // ── 分割线 ──
    readonly property color divider:       "#2e2921"

    // ── 字体 ──
    readonly property string fontDisplay:  "Noto Serif SC"       // 标题/正文（衬线）
    readonly property string fontUi:       "Microsoft YaHei UI"  // UI 控件（无衬线）

    // ── 字号 ──
    readonly property int sizeCaption:  11
    readonly property int sizeUi:       13
    readonly property int sizeBody:     15
    readonly property int sizeTitle:    18
    readonly property int sizeDisplay:  22

    // ── 间距 ──
    readonly property int gapXs: 4
    readonly property int gapSm: 8
    readonly property int gapMd: 12
    readonly property int gapLg: 16
    readonly property int gapXl: 24

    // ── 圆角 ──
    readonly property int radiusSm: 6
    readonly property int radiusMd: 10

    // ── 动画时长 ──
    readonly property int animFast: 120
    readonly property int animNormal: 220
}
```

注意：`bgDeep`/`bgPanel` 已删除，引用它们的文件（MainWindow/StatusBar/ReaderPanel）在 Task 4/6/7 中同步更新——**Task 3-4 必须连续完成后再构建**。

---

### Task 4: 三栏布局（SidebarPanel + MainWindow + CMake）

**Files:**
- Create: `src/novelagent_qt/qml/SidebarPanel.qml`
- Delete: `src/novelagent_qt/qml/ProjectPanel.qml`、`src/novelagent_qt/qml/SessionPanel.qml`
- Modify: `src/novelagent_qt/qml/MainWindow.qml`
- Modify: `CMakeLists.txt:110-120`

- [x] **Step 1: 新建 SidebarPanel.qml**

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// SidebarPanel — 左侧栏：应用标题 + 当前项目卡片 + 会话列表 + 设置入口。
Rectangle {
    id: root
    color: Theme.bgSidebar

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── 应用标题 ──
        Label {
            Layout.leftMargin: Theme.gapLg
            Layout.topMargin: Theme.gapLg
            Layout.bottomMargin: Theme.gapMd
            text: "墨染"
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.sizeDisplay
            font.weight: Font.Bold
            color: Theme.textPrimary
        }

        // ── 当前项目卡片 ──
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapSm
            Layout.rightMargin: Theme.gapSm
            Layout.bottomMargin: Theme.gapMd
            height: 56
            radius: Theme.radiusMd
            color: Theme.bgElevated
            border.width: 1
            border.color: Theme.divider

            ColumnLayout {
                anchors { fill: parent; leftMargin: Theme.gapMd; rightMargin: Theme.gapMd }
                spacing: 2
                Label {
                    text: "当前项目"
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeCaption
                    color: Theme.textFaint
                }
                Label {
                    text: bridge.projectName
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.sizeUi
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

        // ── 会话标题行 ──
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapLg
            Layout.rightMargin: Theme.gapSm
            Layout.topMargin: Theme.gapMd
            Layout.bottomMargin: Theme.gapSm

            Label {
                text: "会话"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                font.weight: Font.DemiBold
                color: Theme.textSecondary
                Layout.fillWidth: true
            }
            Button {
                id: newSessionBtn
                text: "+ 新建"
                onClicked: bridge.newSession()
                contentItem: Text {
                    text: newSessionBtn.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeCaption
                    color: Theme.accent
                }
                background: Rectangle {
                    radius: Theme.radiusSm
                    color: newSessionBtn.hovered ? Theme.bgHover : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }
            }
        }

        // ── 会话列表 ──
        ListView {
            id: sessionList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            leftMargin: Theme.gapSm
            rightMargin: Theme.gapSm
            spacing: 2

            model: ListModel {
                ListElement { name: "当前会话"; active: true }
            }

            delegate: Rectangle {
                width: sessionList.width - Theme.gapSm * 2
                height: 36
                radius: Theme.radiusSm
                color: model.active ? Theme.bgHover
                     : sessionMa.containsMouse ? Theme.bgHover : "transparent"

                RowLayout {
                    anchors { fill: parent; leftMargin: Theme.gapMd; rightMargin: Theme.gapMd }
                    spacing: Theme.gapSm
                    Rectangle {
                        width: 7; height: 7; radius: 3.5
                        color: model.active ? Theme.agentTint : Theme.textFaint
                    }
                    Label {
                        text: model.name
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.sizeUi
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                MouseArea {
                    id: sessionMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

        // ── 底部设置入口 ──
        Rectangle {
            Layout.fillWidth: true
            height: 44
            color: "transparent"

            Rectangle {
                anchors { right: parent.right; rightMargin: Theme.gapMd; verticalCenter: parent.verticalCenter }
                width: 32
                height: 32
                radius: Theme.radiusSm
                color: settingsMa.containsMouse ? Theme.bgHover : "transparent"

                ToolTip.visible: settingsMa.containsMouse
                ToolTip.text: "设置功能开发中"
                ToolTip.delay: 300

                Label {
                    anchors.centerIn: parent
                    text: "\u2699"
                    font.pixelSize: 18
                    color: settingsMa.containsMouse ? Theme.textPrimary : Theme.textSecondary
                }

                MouseArea {
                    id: settingsMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }
    }
}
```

- [x] **Step 2: 删除旧面板**

删除 `src/novelagent_qt/qml/ProjectPanel.qml` 和 `src/novelagent_qt/qml/SessionPanel.qml`（用 DeleteFile 工具，勿用 shell rm）。

- [x] **Step 3: MainWindow.qml 修改**

三处修改：

(a) `color: Theme.bgDeep` → `color: Theme.bgSidebar`；标题拖拽区 Rectangle 的 `color: Theme.bgDeep` → `color: Theme.bgSidebar`。

(b) 三个窗口按钮替换为内联组件。在 `ColumnLayout {` 之前（`ApplicationWindow` 直接子级）添加：

```qml
    component WinButton: Button {
        property color hoverColor: Theme.bgHover
        property color hoverText: Theme.textPrimary
        flat: true
        implicitWidth: 36
        implicitHeight: 28
        contentItem: Text {
            text: parent.text
            font.pixelSize: 12
            color: parent.hovered ? parent.hoverText : Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.radiusSm
            color: parent.hovered ? parent.hoverColor : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.animFast } }
        }
    }
```

原 RowLayout 中三个 `Button {...}`（约 54-100 行）整体替换为：

```qml
                WinButton { text: "\u2500"; onClicked: window.showMinimized() }
                WinButton {
                    text: "\u25A1"
                    onClicked: {
                        if (window.visibility === Window.Maximized)
                            window.showNormal()
                        else
                            window.showMaximized()
                    }
                }
                WinButton {
                    text: "\u2715"
                    hoverColor: Theme.accent
                    hoverText: "#f5efe2"
                    onClicked: window.close()
                }
```

(c) SplitView 改三栏并加分割线样式：

```qml
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            handle: Rectangle {
                implicitWidth: 1
                implicitHeight: 1
                color: Theme.divider
            }

            SidebarPanel {
                SplitView.preferredWidth: 240
                SplitView.minimumWidth: 200
                SplitView.maximumWidth: 320
            }

            AgentPanel {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 400
            }

            ReaderPanel {
                id: readerPanel
                SplitView.preferredWidth: 420
                SplitView.minimumWidth: 300
            }
        }
```

- [x] **Step 4: CMakeLists.txt 更新 QML 文件列表**

`NOVELAGENT_QML_FILES`（110-120 行）替换为：

```cmake
    set(NOVELAGENT_QML_FILES
        src/novelagent_qt/qml/MainWindow.qml
        src/novelagent_qt/qml/SidebarPanel.qml
        src/novelagent_qt/qml/AgentPanel.qml
        src/novelagent_qt/qml/ChatBubble.qml
        src/novelagent_qt/qml/ToolCallCard.qml
        src/novelagent_qt/qml/ReaderPanel.qml
        src/novelagent_qt/qml/StatusBar.qml
        src/novelagent_qt/qml/Theme.qml
        src/novelagent_qt/qml/qmldir
    )
```

（`ToolCallCard.qml` 在 Task 5 创建；本 Task 结束时先创建空占位会导致构建失败，故 **Task 4 与 Task 5/6/7 全部完成后统一构建**，见 Task 8。）

---

### Task 5: 对话区增强（ToolCallCard + AgentPanel + ChatBubble）

**Files:**
- Create: `src/novelagent_qt/qml/ToolCallCard.qml`
- Modify: `src/novelagent_qt/qml/AgentPanel.qml`（整文件替换）
- Modify: `src/novelagent_qt/qml/ChatBubble.qml`（整文件替换）

- [x] **Step 1: 新建 ToolCallCard.qml**

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ToolCallCard — 工具调用状态卡片（居左窄条）。
// status: "running"（⚙ 旋转）| "ok"（✓ 青竹）| "error"（✕ danger）
Item {
    id: root

    property string toolName: ""
    property string status: "running"

    implicitHeight: card.height

    onStatusChanged: if (status !== "running") icon.rotation = 0

    Rectangle {
        id: card
        width: row.implicitWidth + Theme.gapMd * 2
        height: 30
        radius: Theme.radiusSm
        color: Theme.bgElevated
        border.width: 1
        border.color: root.status === "error" ? Theme.danger : Theme.divider

        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: Theme.gapSm

            Label {
                id: icon
                text: root.status === "running" ? "\u2699"
                    : root.status === "ok" ? "\u2713" : "\u2715"
                font.pixelSize: Theme.sizeUi
                color: root.status === "error" ? Theme.danger : Theme.agentTint

                RotationAnimation on rotation {
                    running: root.status === "running"
                    from: 0; to: 360
                    duration: 1600
                    loops: Animation.Infinite
                }
            }

            Label {
                text: root.toolName + (root.status === "running" ? " · 执行中…"
                    : root.status === "ok" ? " · 完成" : " · 失败")
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeUi
                color: root.status === "error" ? Theme.danger : Theme.textSecondary
            }
        }
    }
}
```

- [x] **Step 2: AgentPanel.qml 整文件替换**

要点：根节点改为 `Rectangle`（bgChat）；chatModel 条目统一携带全部字段（ListModel 角色集由首条目固定，缺字段会静默失效）；delegate 用 Loader 按 `type` 分发（Component 必须声明在 Loader 内部，保证 `model` 上下文可见）；空状态 3 个建议卡；输入区加快捷键提示。

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// AgentPanel — 中栏：对话流（消息气泡 / 工具卡片）+ 空状态建议 + 输入区。
Rectangle {
    id: root
    color: Theme.bgChat

    // chatModel 条目统一字段：
    //   type: "message" | "tool"
    //   role/content/reasoning/streaming — message 条目使用
    //   toolName/toolStatus("running"|"ok"|"error") — tool 条目使用
    ListModel { id: chatModel }

    // 仅当「最后一条」是 streaming 中的 assistant 消息时返回其下标，否则 -1。
    // （工具卡片插入后，后续 token 应开启新气泡，而非回写旧气泡。）
    function lastStreamingAssistant() {
        var idx = chatModel.count - 1
        if (idx < 0) return -1
        var it = chatModel.get(idx)
        return (it.type === "message" && it.role === "assistant" && it.streaming) ? idx : -1
    }

    function appendAssistant(content, reasoning) {
        chatModel.append({ type: "message", role: "assistant", content: content,
                           reasoning: reasoning, streaming: true, toolName: "", toolStatus: "" })
    }

    function finalizeRunningTools(status) {
        for (var i = 0; i < chatModel.count; ++i) {
            var it = chatModel.get(i)
            if (it.type === "tool" && it.toolStatus === "running")
                chatModel.setProperty(i, "toolStatus", status)
        }
    }

    function sendCurrentMessage() {
        var text = inputField.text.trim()
        if (text.length === 0 || bridge.busy) return

        chatModel.append({ type: "message", role: "user", content: text,
                           reasoning: "", streaming: false, toolName: "", toolStatus: "" })
        appendAssistant("", "")

        inputField.text = ""
        bridge.sendMessage(text)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ListView {
            id: chatView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            interactive: true
            spacing: Theme.gapMd
            topMargin: Theme.gapLg
            bottomMargin: Theme.gapLg
            leftMargin: Theme.gapLg
            rightMargin: Theme.gapLg

            model: chatModel
            delegate: Loader {
                width: chatView.width - chatView.leftMargin - chatView.rightMargin
                height: item ? item.implicitHeight : 0
                sourceComponent: model.type === "tool" ? toolComp : msgComp

                Component {
                    id: msgComp
                    ChatBubble {
                        role: model.role
                        content: model.content
                        reasoning: model.reasoning
                        streaming: model.streaming === true
                    }
                }
                Component {
                    id: toolComp
                    ToolCallCard {
                        toolName: model.toolName
                        status: model.toolStatus
                    }
                }
            }

            add: Transition {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animNormal }
            }

            property bool userAtBottom: true

            onContentYChanged: {
                if (moving || flicking)
                    userAtBottom = atYEnd
            }

            onContentHeightChanged: {
                if (userAtBottom)
                    contentY = Math.max(0, contentHeight - height)
            }

            onHeightChanged: {
                if (userAtBottom)
                    contentY = Math.max(0, contentHeight - height)
            }

            // ── 空状态 ──
            Column {
                anchors.centerIn: parent
                visible: chatModel.count === 0
                spacing: Theme.gapLg

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "墨染"
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.sizeDisplay + 12
                    font.weight: Font.Bold
                    color: Theme.textPrimary
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "你的 AI 小说创作伙伴 — 构思、写作、管理设定"
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeUi
                    color: Theme.textFaint
                }

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.gapSm

                    Repeater {
                        model: [
                            { title: "开始一部新小说", prompt: "我想开始一部新小说，请帮我构思大纲、角色和世界观" },
                            { title: "创作新章节",     prompt: "根据现有大纲和设定，继续写下一章" },
                            { title: "构建世界观",     prompt: "帮我完善这部小说的世界观设定" }
                        ]
                        delegate: Rectangle {
                            width: 320
                            height: 44
                            radius: Theme.radiusMd
                            color: cardMa.containsMouse ? Theme.bgHover : Theme.bgElevated
                            border.width: 1
                            border.color: Theme.divider
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }

                            Label {
                                anchors { left: parent.left; leftMargin: Theme.gapMd; verticalCenter: parent.verticalCenter }
                                text: modelData.title
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.sizeUi
                                color: Theme.textPrimary
                            }
                            Label {
                                anchors { right: parent.right; rightMargin: Theme.gapMd; verticalCenter: parent.verticalCenter }
                                text: "\u2192"
                                font.pixelSize: Theme.sizeUi
                                color: Theme.textFaint
                            }
                            MouseArea {
                                id: cardMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    inputField.text = modelData.prompt
                                    inputField.forceActiveFocus()
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── 输入区 ──
        Rectangle {
            id: inputRect
            Layout.fillWidth: true
            Layout.margins: Theme.gapMd
            implicitHeight: inputField.height + sendRow.height + Theme.gapMd * 2 + Theme.gapSm
            radius: Theme.radiusMd
            color: Theme.bgElevated
            border.color: inputField.activeFocus ? Theme.accent : Theme.divider
            border.width: 1

            Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

            MouseArea {
                anchors.fill: parent
                onClicked: (mouse) => { inputField.forceActiveFocus() }
            }

            TextArea {
                id: inputField
                anchors {
                    top: parent.top
                    left: parent.left
                    right: parent.right
                    topMargin: Theme.gapMd
                    leftMargin: Theme.gapMd
                    rightMargin: Theme.gapMd
                }
                height: Math.min(Math.max(implicitHeight, 24), 120)
                placeholderText: ""
                color: Theme.textPrimary
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeBody
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                leftPadding: 0
                rightPadding: 0
                topPadding: 4
                bottomPadding: 4
                background: Item {}

                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Return && !event.modifiers) {
                        event.accepted = true
                        root.sendCurrentMessage()
                    }
                }
            }

            Label {
                anchors {
                    left: inputField.left
                    top: inputField.top
                    topMargin: inputField.topPadding
                }
                visible: inputField.text.length === 0 && !inputField.activeFocus
                text: "输入指令或问题..."
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeBody
                color: Theme.textFaint
            }

            RowLayout {
                id: sendRow
                anchors {
                    top: inputField.bottom
                    left: parent.left
                    right: parent.right
                    topMargin: Theme.gapSm
                    leftMargin: Theme.gapMd
                    rightMargin: Theme.gapMd
                    bottomMargin: Theme.gapSm
                }

                Label {
                    text: "Enter 发送 · Shift+Enter 换行"
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeCaption
                    color: Theme.textFaint
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: sendBtn
                    text: bridge.busy ? "取消" : "发送"
                    enabled: bridge.busy || inputField.text.trim().length > 0
                    onClicked: {
                        if (bridge.busy) {
                            bridge.cancelRequest()
                        } else {
                            root.sendCurrentMessage()
                        }
                    }

                    contentItem: Text {
                        text: sendBtn.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.sizeUi
                        color: sendBtn.enabled ? "#f5efe2" : Theme.textFaint
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        implicitWidth: 64
                        implicitHeight: 32
                        radius: Theme.radiusSm
                        color: bridge.busy ? Theme.danger
                             : sendBtn.enabled ? Theme.accent
                             : Theme.bgHover
                        Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    }
                }
            }
        }
    }

    Connections {
        target: bridge

        function onTokenReceived(delta) {
            var idx = root.lastStreamingAssistant()
            if (idx >= 0)
                chatModel.setProperty(idx, "content", chatModel.get(idx).content + delta)
            else
                root.appendAssistant(delta, "")
        }

        function onReasoningReceived(delta) {
            var idx = root.lastStreamingAssistant()
            if (idx >= 0)
                chatModel.setProperty(idx, "reasoning", chatModel.get(idx).reasoning + delta)
            else
                root.appendAssistant("", delta)
        }

        function onToolCallStarted(toolName) {
            var idx = root.lastStreamingAssistant()
            if (idx >= 0) {
                var it = chatModel.get(idx)
                if (it.content.length === 0 && it.reasoning.length === 0)
                    chatModel.remove(idx)   // 空占位直接移除，避免残留空气泡
                else
                    chatModel.setProperty(idx, "streaming", false)
            }
            chatModel.append({ type: "tool", role: "", content: "", reasoning: "",
                               streaming: false, toolName: toolName, toolStatus: "running" })
        }

        function onToolCallFinished(toolName, ok) {
            for (var i = chatModel.count - 1; i >= 0; --i) {
                var it = chatModel.get(i)
                if (it.type === "tool" && it.toolName === toolName && it.toolStatus === "running") {
                    chatModel.setProperty(i, "toolStatus", ok ? "ok" : "error")
                    return
                }
            }
        }

        function onResponseComplete(fullText) {
            root.finalizeRunningTools("ok")
            var idx = root.lastStreamingAssistant()
            if (idx >= 0) {
                var it = chatModel.get(idx)
                if (it.content.length === 0 && it.reasoning.length === 0)
                    chatModel.remove(idx)
                else
                    chatModel.setProperty(idx, "streaming", false)
            }
        }

        function onErrorOccurred(message) {
            root.finalizeRunningTools("error")
            var idx = root.lastStreamingAssistant()
            if (idx >= 0)
                chatModel.setProperty(idx, "streaming", false)
            chatModel.append({ type: "message", role: "assistant",
                               content: "⚠ " + message, reasoning: "",
                               streaming: false, toolName: "", toolStatus: "" })
        }
    }
}
```

- [x] **Step 3: ChatBubble.qml 整文件替换**

新增 `reasoning` 属性 + 折叠条 + hover 复制按钮（QML 无剪贴板 API，用隐藏 TextEdit selectAll+copy）：

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    property string role: "user"
    property string content: ""
    property string reasoning: ""
    property bool streaming: false
    property bool reasoningExpanded: false

    width: parent ? parent.width : 0
    spacing: Theme.gapXs

    readonly property bool isUser: role === "user"
    readonly property string displayText: content.replace(/\n{2,}/g, "\n").replace(/\n+$/, "")
    readonly property string formattedText: isUser ? displayText : content

    Label {
        Layout.alignment: root.isUser ? Qt.AlignRight : Qt.AlignLeft
        Layout.leftMargin: root.isUser ? 0 : Theme.gapSm
        Layout.rightMargin: root.isUser ? Theme.gapSm : 0
        text: root.isUser ? "你" : "墨染"
        font.family: Theme.fontUi
        font.pixelSize: Theme.sizeCaption
        font.weight: Font.DemiBold
        color: root.isUser ? Theme.accent : Theme.agentTint
    }

    // ── 思考过程折叠条（仅 assistant 且 reasoning 非空）──
    Rectangle {
        visible: !root.isUser && root.reasoning.length > 0
        Layout.alignment: Qt.AlignLeft
        Layout.leftMargin: Theme.gapSm
        width: reasoningHeader.implicitWidth + Theme.gapMd * 2
        height: 24
        radius: Theme.radiusSm
        color: reasoningMa.containsMouse ? Theme.bgHover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.animFast } }

        Row {
            id: reasoningHeader
            anchors.centerIn: parent
            spacing: Theme.gapXs
            Label {
                text: "\ud83d\udcad 思考过程"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textFaint
            }
            Label {
                text: root.reasoningExpanded ? "\u25be" : "\u25b8"
                font.pixelSize: Theme.sizeCaption
                color: Theme.textFaint
            }
        }

        MouseArea {
            id: reasoningMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.reasoningExpanded = !root.reasoningExpanded
        }
    }

    // ── 展开的思考过程正文（左侧竖线 + 弱化小字）──
    Rectangle {
        visible: !root.isUser && root.reasoningExpanded && root.reasoning.length > 0
        Layout.leftMargin: Theme.gapSm
        Layout.preferredWidth: root.width * 0.82
        implicitHeight: reasoningText.implicitHeight + Theme.gapSm * 2
        color: "transparent"

        Rectangle {
            width: 2
            height: parent.height
            color: Theme.divider
        }

        Text {
            id: reasoningText
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                leftMargin: Theme.gapMd
                topMargin: Theme.gapSm
            }
            text: root.reasoning
            wrapMode: Text.Wrap
            font.family: Theme.fontUi
            font.pixelSize: Theme.sizeCaption + 1
            lineHeight: 1.5
            color: Theme.textFaint
        }
    }

    Rectangle {
        id: bubbleRect
        Layout.alignment: root.isUser ? Qt.AlignRight : Qt.AlignLeft
        Layout.maximumWidth: root.width * 0.82
        Layout.leftMargin: root.isUser ? 0 : Theme.gapSm
        Layout.rightMargin: root.isUser ? Theme.gapSm : 0

        implicitWidth: Math.min(bubbleText.maxWidth, Math.max(40, textMeasurer.contentWidth)) + Theme.gapMd * 2
        implicitHeight: bubbleText.contentHeight + Theme.gapXs * 2
        radius: Theme.radiusMd
        color: root.isUser ? Theme.accentSoft : Theme.bgElevated
        border.width: root.isUser ? 0 : 1
        border.color: Theme.divider

        Text {
            id: textMeasurer
            visible: false
            text: bubbleText.text
            font: bubbleText.font
            textFormat: bubbleText.textFormat
            wrapMode: Text.NoWrap
        }

        Text {
            id: bubbleText
            x: Theme.gapMd
            y: Theme.gapXs
            property real maxWidth: root.width * 0.82 - Theme.gapMd * 2
            width: bubbleRect.width - Theme.gapMd * 2
            text: root.formattedText + (!root.isUser && root.streaming ? "▍" : "")
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.sizeBody
            lineHeight: root.isUser ? 20 : 1.4
            lineHeightMode: root.isUser ? Text.FixedHeight : Text.ProportionalHeight
            wrapMode: Text.Wrap
            textFormat: root.isUser ? Text.PlainText : Text.MarkdownText
            color: Theme.textPrimary
            linkColor: Theme.accent
            padding: 0

            SequentialAnimation on color {
                running: root.streaming
                loops: Animation.Infinite
                ColorAnimation { to: Theme.textSecondary; duration: 400 }
                ColorAnimation { to: Theme.textPrimary; duration: 400 }
            }
        }

        // hover 检测（不拦截点击）
        MouseArea {
            id: bubbleMa
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
        }

        // ── 复制按钮（hover 浮现，右下角）──
        Rectangle {
            id: copyBtn
            anchors { right: parent.right; bottom: parent.bottom; margins: Theme.gapXs }
            width: copyLabel.implicitWidth + Theme.gapSm * 2
            height: 22
            radius: Theme.radiusSm
            color: Theme.bgHover
            border.width: 1
            border.color: Theme.divider
            visible: (bubbleMa.containsMouse || copyMa.containsMouse) && !root.streaming
                     && root.content.length > 0
            opacity: 0.95

            property bool copied: false

            Label {
                id: copyLabel
                anchors.centerIn: parent
                text: copyBtn.copied ? "已复制" : "复制"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: copyBtn.copied ? Theme.agentTint : Theme.textSecondary
            }

            MouseArea {
                id: copyMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    clipboardHelper.text = root.content
                    clipboardHelper.selectAll()
                    clipboardHelper.copy()
                    copyBtn.copied = true
                    copiedTimer.restart()
                }
            }

            Timer {
                id: copiedTimer
                interval: 1200
                onTriggered: copyBtn.copied = false
            }
        }

        // 隐藏 TextEdit：承载剪贴板复制
        TextEdit {
            id: clipboardHelper
            visible: false
        }
    }
}
```

（构建验证统一在 Task 8。）

---

### Task 6: ReaderPanel 章节下拉 + 底部字数

**Files:**
- Modify: `src/novelagent_qt/qml/ReaderPanel.qml`（整文件替换）

- [x] **Step 1: ReaderPanel.qml 整文件替换**

要点：根节点改 `Rectangle`（bgReader，同时消除旧 `Theme.bgPanel` 引用）；标题栏换成章节下拉选择器（Popup + 章节列表）；数据来自 `bridge.chapterList()` / `bridge.loadChapter(id)`；`onChaptersChanged` 刷新并保持选中（被删则回到占位）；字数移到底部右下角。

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ReaderPanel — 右栏：章节选择 + 只读阅读视图。
// Markdown 渲染，衬线字体，行高 1.9，左右留白书页效果。
Rectangle {
    id: root
    color: Theme.bgReader

    property var chapters: []
    property int currentIndex: -1
    property string chapterContent: ""

    readonly property string currentTitle:
        (currentIndex >= 0 && currentIndex < chapters.length)
            ? chapters[currentIndex].title : "暂无章节"

    // 刷新章节列表；保持当前选中（按 id 对齐），选中项被删则回到占位。
    function reload() {
        var keepId = (currentIndex >= 0 && currentIndex < chapters.length)
                     ? chapters[currentIndex].id : ""
        chapters = bridge.chapterList()
        var idx = -1
        if (keepId !== "") {
            for (var i = 0; i < chapters.length; ++i) {
                if (chapters[i].id === keepId) { idx = i; break }
            }
        }
        currentIndex = idx
        if (idx < 0)
            chapterContent = ""
    }

    function selectChapter(i) {
        currentIndex = i
        chapterContent = bridge.loadChapter(chapters[i].id)
        chapterPopup.close()
        flick.contentY = 0
    }

    Component.onCompleted: reload()

    Connections {
        target: bridge
        function onChaptersChanged() { root.reload() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── 章节选择栏 ──
        Rectangle {
            Layout.fillWidth: true
            height: 48
            color: "transparent"

            Rectangle {
                id: selectorBtn
                anchors { left: parent.left; leftMargin: Theme.gapLg; verticalCenter: parent.verticalCenter }
                width: Math.min(selectorRow.implicitWidth + Theme.gapMd * 2,
                                parent.width - Theme.gapLg * 2)
                height: 32
                radius: Theme.radiusSm
                color: (selectorMa.containsMouse || chapterPopup.visible) ? Theme.bgHover : "transparent"
                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                RowLayout {
                    id: selectorRow
                    anchors { left: parent.left; leftMargin: Theme.gapMd; verticalCenter: parent.verticalCenter }
                    spacing: Theme.gapSm

                    Label {
                        text: root.currentTitle
                        font.family: Theme.fontDisplay
                        font.pixelSize: Theme.sizeTitle
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                        Layout.maximumWidth: root.width - 120
                    }
                    Label {
                        text: "\u25be"
                        font.pixelSize: Theme.sizeUi
                        color: Theme.textSecondary
                    }
                }

                MouseArea {
                    id: selectorMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: chapterPopup.open()
                }

                Popup {
                    id: chapterPopup
                    y: selectorBtn.height + Theme.gapXs
                    width: 300
                    height: Math.min(Math.max(chapterListView.contentHeight, 48) + Theme.gapSm * 2, 360)
                    padding: Theme.gapSm

                    background: Rectangle {
                        radius: Theme.radiusMd
                        color: Theme.bgElevated
                        border.width: 1
                        border.color: Theme.divider
                    }

                    contentItem: ListView {
                        id: chapterListView
                        clip: true
                        model: root.chapters
                        spacing: 2
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: Rectangle {
                            width: chapterListView.width
                            height: 36
                            radius: Theme.radiusSm
                            color: (index === root.currentIndex || itemMa.containsMouse)
                                   ? Theme.bgHover : "transparent"

                            RowLayout {
                                anchors { fill: parent; leftMargin: Theme.gapMd; rightMargin: Theme.gapMd }
                                spacing: Theme.gapSm

                                Label {
                                    text: modelData.title
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.sizeUi
                                    color: Theme.textPrimary
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: modelData.wordCount > 0 ? modelData.wordCount + " 字" : ""
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.sizeCaption
                                    color: Theme.textFaint
                                }
                            }

                            MouseArea {
                                id: itemMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.selectChapter(index)
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: root.chapters.length === 0
                            text: "暂无章节"
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.sizeUi
                            color: Theme.textFaint
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

        // ── 正文阅读区 ──
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: readerText.implicitHeight + Theme.gapXl * 2
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            Text {
                id: readerText
                anchors {
                    top: parent.top
                    left: parent.left
                    right: parent.right
                    margins: Theme.gapXl
                }
                text: root.chapterContent.length > 0
                      ? root.chapterContent
                      : "从上方选择章节，或让墨染生成内容后在此阅读。"
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.sizeBody + 1
                lineHeight: 1.9
                wrapMode: Text.Wrap
                textFormat: Text.MarkdownText
                color: root.chapterContent.length > 0
                       ? Theme.textPrimary
                       : Theme.textFaint
            }
        }

        // ── 底部字数 ──
        Rectangle {
            Layout.fillWidth: true
            height: 26
            color: "transparent"

            Rectangle {
                anchors { top: parent.top; left: parent.left; right: parent.right }
                height: 1
                color: Theme.divider
            }

            Label {
                anchors { right: parent.right; rightMargin: Theme.gapLg; verticalCenter: parent.verticalCenter }
                text: root.chapterContent.length > 0 ? root.chapterContent.length + " 字" : ""
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textFaint
            }
        }
    }
}
```

（构建验证统一在 Task 8。`bridge.chapterList()` 返回的 QVariantList 在 QML 中作 JS 数组使用，`.length`/下标访问均可。）

---

### Task 7: StatusBar 进度条 + 合并 provider·model

**Files:**
- Modify: `src/novelagent_qt/qml/StatusBar.qml`

- [x] **Step 1: 根背景换 token**

`color: Theme.bgDeep` → `color: Theme.bgSidebar`（bgDeep 已在 Task 3 删除）。

- [x] **Step 2: 上下文百分比改为迷你进度条**

将现有「上下文百分比」Label（`text: "上下文: " + bridge.contextPercent + "%"` 那段）替换为：

```qml
        // 上下文占用：迷你进度条 + 百分比（>80% 变琥珀警示）
        RowLayout {
            spacing: Theme.gapXs

            Rectangle {
                width: 60
                height: 4
                radius: 2
                color: Theme.bgHover

                Rectangle {
                    width: parent.width * Math.min(bridge.contextPercent, 100) / 100
                    height: parent.height
                    radius: 2
                    color: bridge.contextPercent > 80 ? Theme.warning : Theme.agentTint
                    Behavior on width { NumberAnimation { duration: Theme.animNormal } }
                }
            }

            Label {
                text: bridge.contextPercent + "%"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: bridge.contextPercent > 80 ? Theme.warning : Theme.textFaint
            }
        }
```

- [x] **Step 3: 合并模型/Provider 两个 Label**

删除「模型名」与「Provider」两个 Label，换为一个：

```qml
        // Provider · 模型
        Label {
            text: bridge.providerName + " · " + bridge.modelName
            font.family: Theme.fontUi
            font.pixelSize: Theme.sizeCaption
            color: Theme.textFaint
        }
```

---

### Task 8: 统一构建 + 回归 + 手动验收

- [x] **Step 1: 构建 GUI**

Run: `cmake --build build --target novelagent_gui`
Expected: 构建成功，无新增警告（QML 打包进 qrc，语法错误在运行时才暴露，故必须配合 Step 3 启动验证）

- [x] **Step 2: 全量回归测试**

Run: `ctest --test-dir build --output-on-failure`
Expected: 全部 PASSED（含 Task 1 新增的 test_core_loop 用例）

- [x] **Step 3: 启动 GUI 手动核对**

Run: `./build/novelagent_gui.exe`（需 MSYS2 DLL 在 PATH，参考 tests 的 `D:/SoftWare/msys2/mingw64/bin`）

核对清单（对应 spec 第 7 节）：
- [ ] 三栏布局：左侧栏（标题+项目卡片+会话+设置齿轮）/ 对话区 / 阅读区，三栏背景深浅层次分明
- [ ] 窗口按钮 hover：✕ 变朱砂红底白字，─ □ 变 bgHover
- [ ] 空状态：「墨染」大字 + 3 建议卡，点击填充输入框并聚焦（不自动发送）
- [ ] 发送消息：用户气泡右侧朱砂底，Agent 气泡左侧；流式光标正常
- [ ] 工具调用（需配置 API Key + 触发工具的指令，如「读一下第一章」）：卡片 ⚙ 执行中 → ✓ 完成；工具后的回复开新气泡
- [ ] reasoning（DeepSeek reasoner 类模型）：气泡上方出现「💭 思考过程」折叠条，点击展开/收起
- [ ] 消息 hover 出现「复制」，点击后变「已复制」且剪贴板内容正确
- [ ] 阅读区：章节下拉展开（用 `examples/test-proj` 验证有章节场景），选中后正文加载、底部字数显示；无章节项目显示「暂无章节」
- [ ] 状态栏：迷你进度条随上下文增长，`provider · model` 合并显示
- [ ] 取消请求：执行中点「取消」，未完成工具卡片置为 ✕ 失败态，无残留「执行中」卡片

- [x] **Step 4: 勾选完成项并汇报**

将本计划文档中全部 checkbox 更新为 `- [x]`；向用户汇报核对结果（含未能验证项及原因，如无 API Key 时的工具卡片项）。

---

## 执行顺序与构建门控

| 阶段 | Task | 构建点 |
|---|---|---|
| C++ 层 | 1 → 2 | Task 1 后跑 test_core_loop；Task 2 后构建 novelagent_gui（QML 未动，仍可运行） |
| QML 层 | 3 → 4 → 5 → 6 → 7 | 中途不构建（token 改名跨文件，中间态必破） |
| 验收 | 8 | 统一构建 + 全量回归 + 手动核对 |

