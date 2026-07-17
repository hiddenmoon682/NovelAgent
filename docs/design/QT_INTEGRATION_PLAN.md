# NovelAgent Qt6 QML 集成方案

> 本文件记录了 NovelAgent 从 CLI 终端模式向 Qt6 QML GUI 的迁移方案。
> UI 层全部使用 QML 声明式开发，C++ 侧只负责业务逻辑桥接。

---

## Context

现有 NovelAgent 是基于 C++20 的 CLI 写作工具，通过 `ReplHandler` / `TerminalGUI` 提供终端交互。
决定放弃 Tauri Web 前端，转向 Qt6 QML 作为 GUI 方案。QML 的声明式 UI + 原生动画能力可以复现
`preview.html` 中的所有前端效果（淡入动画、打字光标、hover 过渡、渐变图标等），且代码量远少于 QtWidgets。

### Qt 版本与路径

- **当前版本：Qt 6.8.3**（安装路径 `D:/QT/QT/6.8.3/mingw_64/`）
- CMake 配置需添加：`set(CMAKE_PREFIX_PATH "D:/QT/QT/6.8.3/mingw_64" ${CMAKE_PREFIX_PATH})`
- 协议：LGPLv3，动态链接，闭源商用无限制

---

## 核心约束

- **核心层（`src/agent/`、`src/llm/`、`src/project/`、`src/retrieval/`、所有工具）零改动**
- cpp-httplib 保留，不迁移 Qt Network
- CLI 模式保留（通过 `--cli` 参数进入）
- Qt UI 全部放在 `src/novelagent_qt/`，不污染其他目录
- **UI 层全部使用 QML**，C++ 只提供桥接对象（不写 QWidget 子类）
- **借助 AI 辅助开发**：QML 页面全部由 AI 生成，开发者只需描述需求即可

---

## 架构概览

```
┌──────────────────────────────────────────────────────┐
│                  QML UI 层（声明式）                    │
│  MainWindow.qml    AgentPanel.qml    ProjectTree.qml  │
│  ChapterEditor.qml  ChatBubble.qml   StatusBar.qml    │
│  NovelSettings.qml  CharacterPanel.qml                 │
└──────────────┬───────────────────────────────────────┘
               │ setContextProperty / qmlRegisterType
               ▼
┌──────────────────────────────────────────────────────┐
│              C++ 桥接层（业务逻辑适配）                  │
│  QmlBridge.h/.cpp  ← 持有 Agent&、Project&             │
│  注册为 QML context property，暴露信号/槽/属性           │
└──────────────┬───────────────────────────────────────┘
               │ 直接调用
               ▼
┌──────────────────────────────────────────────────────┐
│           NovelAgent 核心层（零改动）                   │
│  Agent / LLMClient / Project / ToolRegistry / ...     │
└──────────────────────────────────────────────────────┘
```

---

## 文件结构

```
src/novelagent_qt/
├── qml/
│   ├── MainWindow.qml           ← 主窗口布局（菜单栏 + 三栏 split 视图）
│   ├── AgentPanel.qml           ← 聊天面板（消息列表 + 输入框）
│   ├── ProjectTree.qml          ← 项目树（章节/角色/设定/规则）
│   ├── ChapterEditor.qml        ← 章节正文编辑器
│   ├── ChatBubble.qml           ← 单条消息气泡组件（左右对齐 + 动画）
│   ├── StatusBar.qml            ← 底部状态栏
│   ├── NovelSettings.qml        ← 设置对话框
│   ├── CharacterPanel.qml       ← 角色详情面板
│   └── Theme.qml                ← 颜色/字体/间距等主题常量
├── QmlBridge.h                  ← C++ ↔ QML 桥接（暴露 Agent / Project 给 QML）
├── QmlBridge.cpp
├── QmlApp.cpp                   ← QML 应用入口（QGuiApplication + QQmlApplicationEngine）
└── QmlApp.h
```

---

## C++ 桥接层设计

### QmlBridge — 单一桥接对象

不要为每个组件建独立的 C++ 类。一个 `QmlBridge` 对象注册到 QML context，通过属性/信号/槽暴露所有功能：

```cpp
class QmlBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
public:
    explicit QmlBridge(agent::Agent& agent, std::shared_ptr<Project> project);

    // ── QML 可调用的槽（Invokable）──
    Q_INVOKABLE void sendMessage(const QString& text);
    Q_INVOKABLE void loadChapter(const QString& chapterId);
    Q_INVOKABLE void saveChapter(const QString& chapterId, const QString& content);
    Q_INVOKABLE void refreshProject();
    Q_INVOKABLE QStringList listChapters();

    // ── QML 监听的信号（流式输出关键路径）──
signals:
    void tokenReceived(const QString& delta);           // 流式 token（逐字追加）
    void reasoningReceived(const QString& delta);       // 推理过程（DeepSeek thinking）
    void responseComplete(const QString& fullText);     // 回复完成
    void chapterLoaded(const QString& chapterId, const QString& content);
    void projectChanged();
    void statusChanged(const QString& text);

private:
    agent::Agent& agent_;
    std::shared_ptr<Project> project_;
};
```

### 流式回调接线

```cpp
void QmlBridge::setupStreamCallbacks() {
    StreamCallbacks cb;
    cb.on_content = [this](const std::string& delta) {
        // 跨线程安全：QML 属性更新必须在主线程
        QMetaObject::invokeMethod(this, [this, d = QString::fromUtf8(delta)] {
            emit tokenReceived(d);
        }, Qt::QueuedConnection);
    };
    cb.on_reasoning = [this](const std::string& delta) {
        QMetaObject::invokeMethod(this, [this, d = QString::fromUtf8(delta)] {
            emit reasoningReceived(d);
        }, Qt::QueuedConnection);
    };
    // ... 在调用 agent_.processUserMessage() 时传入
}
```

### QML 侧使用

```qml
// AgentPanel.qml
ColumnLayout {
    ListView {
        id: chatView
        model: chatModel
        delegate: ChatBubble {
            text: model.message
            isUser: model.role === "user"
        }
        add: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 300 }
        }
    }

    RowLayout {
        TextField {
            id: inputField
            Layout.fillWidth: true
            onAccepted: bridge.sendMessage(text)
        }
        Button { text: "发送"; onClicked: bridge.sendMessage(inputField.text) }
    }

    Connections {
        target: bridge
        function onTokenReceived(delta) {
            // 追加到当前 AI 回复
            chatModel.appendDelta(delta)
        }
    }
}
```

---

## QML 组件设计

### 1. 主窗口布局（MainWindow.qml）

与 `preview.html` 结构对应，使用 QML 原生布局组件：

```qml
ApplicationWindow {
    width: 1400; height: 900
    title: "墨染 · AI小说创作助手"

    menuBar: MenuBar {
        Menu { title: "项目"; MenuItem { text: "新建" } ... }
        Menu { title: "编辑"; ... }
        Menu { title: "工具"; ... }
    }

    SplitView {
        anchors.fill: parent
        // 左侧：项目树（可切换章节/人物）
        Pane { implicitWidth: 260; ProjectTree { ... } }
        // 中央：编辑器 + 聊天面板
        SplitView {
            orientation: Qt.Vertical
            ChapterEditor { ... }
            AgentPanel { implicitHeight: 300 }
        }
        // 右侧：详情面板
        Pane { implicitWidth: 280; CharacterPanel { ... } }
    }

    footer: StatusBar { ... }
}
```

### 2. 聊天面板（AgentPanel.qml）

```qml
ColumnLayout {
    spacing: 0

    ListView {
        id: chatView
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 12
        // 底部自动滚动
        onContentHeightChanged: {
            if (contentHeight > height)
                contentY = contentHeight - height
        }

        model: ListModel { id: chatModel }
        delegate: ChatBubble {}

        // 淡入动画
        add: Transition {
            NumberAnimation {
                property: "opacity"; from: 0; to: 1; duration: 300
            }
        }
    }

    // 输入区
    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 12

        TextField {
            id: inputField
            Layout.fillWidth: true
            placeholderText: "输入指令或问题..."
            onAccepted: sendMessage()
        }
        Button {
            text: "发送"
            enabled: inputField.text.length > 0
            onClicked: sendMessage()
        }
    }
}
```

### 3. 消息气泡（ChatBubble.qml）

```qml
Rectangle {
    property string text
    property bool isUser: false

    width: parent.width
    height: bubbleContent.height + 24
    color: "transparent"

    RowLayout {
        anchors { left: parent.left; right: parent.right; margins: 12 }
        LayoutMirroring.enabled: isUser   // 用户消息靠右

        Rectangle {
            Layout.maximumWidth: parent.width * 0.75
            radius: 8
            color: isUser ? "#6366f1" : "#1f2937"

            Text {
                anchors { fill: parent; margins: 12 }
                text: parent.parent.text
                color: "white"
                font.family: "Noto Serif SC"
                wrapMode: Text.Wrap
            }
        }
        Item { Layout.fillWidth: true }  // 占位，实现左右对齐
    }
}
```

### 4. 项目树（ProjectTree.qml）

```qml
ColumnLayout {
    Text { text: "章节大纲"; font.bold: true; ... }

    TextField { placeholderText: "搜索章节..." }

    TreeView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        model: chapterTreeModel  // 从 bridge 获取
        delegate: Item {
            // 章节项：编号 + 标题 + hover 操作按钮
            // 参考 preview.html 的 chapter-item 样式
        }
    }
}
```

### 5. 章节编辑器（ChapterEditor.qml）

```qml
ColumnLayout {
    Flickable {
        Layout.fillWidth: true
        Layout.fillHeight: true
        TextArea {
            id: editor
            font { family: "Noto Serif SC"; pixelSize: 16 }
            wrapMode: TextEdit.Wrap
            placeholderText: "在此输入章节正文..."
            onTextChanged: {
                wordCount.text = "字数: " + text.length
            }
        }
    }

    RowLayout {
        Label { id: wordCount; text: "字数: 0" }
        Item { Layout.fillWidth: true }
        Button { text: "保存" }
    }
}
```

### 6. 主题常量（Theme.qml）

```qml
pragma Singleton
QtObject {
    readonly property color primary: "#6366f1"
    readonly property color sidebarBg: "#111827"
    readonly property color sidebarText: "#d1d5db"
    readonly property color editorBg: "#f9fafb"
    readonly property string fontFamily: "Noto Serif SC"
    readonly property int fontSize: 16
    readonly property real lineHeight: 1.9
}
```

---

## 窗口布局示意

```
┌─────────────────────────────────────────────────────────┐
│ 菜单栏: 项目 编辑 工具 帮助                              │
├────────┬───────────────────────┬────────────────────────┤
│        │                       │                        │
│ 左侧    │  中央 SplitView      │  右侧详情面板          │
│ 项目树  │  ├─ 章节编辑器       │  （角色/设定详情，      │
│ 章节    │  ├─ 大纲视图         │   起步阶段占位）        │
│ 角色    │  └─ 卷/剧情线视图    │                        │
│ 设定    │                       │                        │
│ 规则    ├───────────────────────┤                        │
│        │  聊天面板（可调高度）  │                        │
│        │  ListView 消息气泡    │                        │
│        │  TextField 输入框     │                        │
├────────┴───────────────────────┴────────────────────────┤
│ 状态栏: 模型名 | Token 用量 | 上下文 % | Provider       │
└─────────────────────────────────────────────────────────┘
```

SplitView 实现可拖拽分区。

---

## 主窗口初始化流程

```
main()
  → AppConfig::load()
  → 解析 CLI 参数
  → --cli 参数? → novelAgent.runRepl()    ← 走现有路径，不改
  → 否则:
      → QGuiApplication app(argc, argv)
      → NovelAgentApp app(provider, project)  ← 同一装配器
      → QmlBridge bridge(app.agent(), app.project())
      → QQmlApplicationEngine engine
      → engine.rootContext()->setContextProperty("bridge", &bridge)
      → engine.load("qrc:/qml/MainWindow.qml")
      → app.exec()
```

**不绕过 NovelAgentApp：** Qt 模式依然走它的组件装配（`setupAgent`、`registerAllTools`、`ContextManager` 初始化等），
只是 UI 层从 `ReplHandler` 换成 QML。

---

## 依赖与 CMake

```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Quick QuickControls2)

set(NOVELAGENT_QT
    src/novelagent_qt/QmlBridge.cpp
    src/novelagent_qt/QmlApp.cpp
)

set(NOVELAGENT_QML
    src/novelagent_qt/qml/MainWindow.qml
    src/novelagent_qt/qml/AgentPanel.qml
    src/novelagent_qt/qml/ProjectTree.qml
    src/novelagent_qt/qml/ChapterEditor.qml
    src/novelagent_qt/qml/ChatBubble.qml
    src/novelagent_qt/qml/StatusBar.qml
    src/novelagent_qt/qml/NovelSettings.qml
    src/novelagent_qt/qml/CharacterPanel.qml
    src/novelagent_qt/qml/Theme.qml
)

# novelagent_qt 作为 OBJECT 库
add_library(novelagent_qt OBJECT ${NOVELAGENT_QT})
target_include_directories(novelagent_qt PUBLIC src)
target_link_libraries(novelagent_qt PUBLIC novelagent_core Qt6::Core Qt6::Quick Qt6::QuickControls2)

# QML 资源文件
qt_add_resources(novelagent_qt "qml"
    PREFIX "/qml"
    FILES ${NOVELAGENT_QML}
)

# 主 exe 链接
target_link_libraries(novelagent PRIVATE
    novelagent_core novelagent_tools novelagent_app novelagent_qt)
```

不需要 `qt_standard_project_setup`，普通 CMake 即可。

---

## 开发方式（AI 辅助）

由于采用纯 QML + AI 辅助开发，工作流如下：

1. **描述需求**：开发者向 AI 描述需要什么页面/组件（如"一个聊天消息气泡，用户消息靠右蓝色，AI 消息靠左深色，带淡入动画"）
2. **AI 生成 QML**：AI 直接输出 QML 代码，放入 `src/novelagent_qt/qml/`
3. **AI 生成 C++ 桥接**：如果新增功能需要 C++ 侧暴露新的槽/信号，AI 同步更新 `QmlBridge`
4. **运行查看**：`cmake --build build && ./build/novelagent` 即时查看效果
5. **迭代**：不满意直接让 AI 修改

开发者完全不需要手动编写 QML 或 C++ UI 代码。

---

## 不做的事（边界明确）

- ❌ 不迁移 Qt Network（保持 cpp-httplib）
- ❌ 不改动核心层任何文件
- ❌ 不删除现有 CLI 层
- ❌ 不改动任何工具自注册宏
- ❌ 不加数据库 / 富文本 / WebEngine
- ❌ 不引入 QtWidgets（全部使用 QML + Qt Quick Controls 2）
- ⚠️ 不追求 pixel-perfect 复现 preview.html —— 风格接近即可，具体细节在开发中逐步完善

---

## C++ ↔ QML 数据交互方式

### 数据流总览

```
┌───────────────────────────────────────────┐
│              QML UI                        │
│                                            │
│  ① 用户输入       ② 流式输出    ③ 列表显示 │
│  (TextField)      (Text追加)   (ListView)  │
│       │                ▲            ▲      │
│       │  信号槽(Qt::   │    Model   │      │
│       │  QueuedConn)   │    绑定    │      │
│       ▼                ─────────────      │
│  QmlBridge (C++ QObject, 注册到 context)   │
│       │                                    │
│       ▼  (直接调用，可能在工作线程)          │
│  Agent / Project / ToolPipeline            │
└───────────────────────────────────────────┘
```

### 三种数据流

| # | 数据流 | 方向 | 方式 | 线程安全 |
|---|--------|------|------|---------|
| ① | 用户输入 → Agent | QML → C++ | `Q_INVOKABLE bridge.sendMessage(text)` | ✅ 同步调用 |
| ② | Agent 流式输出 → QML | C++ → QML | `emit tokenReceived(delta)` via `Qt::QueuedConnection` | ✅ 跨线程安全 |
| ③ | 项目数据（章节/角色列表） | 双向 | `QAbstractListModel` + `Q_INVOKABLE` | ⚠️ 写入需在主线程 |

### 采用的方案

**单一 `QmlBridge` QObject + `setContextProperty` 为主，`QAbstractListModel` 做列表数据。**

不使用 `qmlRegisterType`、QML modules 或 `QQuickPaintedItem` 等复杂机制。理由：

| 理由 | 说明 |
|------|------|
| **NovelAgent 交互模式简单** | 核心就是"用户发消息 → Agent 回答 → 显示聊天 + 操作项目树"，不需要复杂的类型注册 |
| **流式输出是核心路径** | 信号槽是 Qt 最成熟的线程安全推送机制，`emit tokenReceived(delta)` + QML `Connections` 是最短路径 |
| **列表用 Model** | 章节列表、角色列表适合 `QAbstractListModel`，QML 的 `ListView` 原生支持 |
| **AI 生成友好** | 单一 `bridge` 对象 + `Connections` 的模式非常模式化，AI 很容易生成正确的 QML 代码 |

### QmlBridge 完整接口设计

```cpp
class QmlBridge : public QObject {
    Q_OBJECT
    // ── 响应式属性 ──
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)

public:
    explicit QmlBridge(agent::Agent& agent, std::shared_ptr<Project> project);

    // ── 请求-响应（QML 调用 C++）──
    Q_INVOKABLE void sendMessage(const QString& text);
    Q_INVOKABLE void loadChapter(const QString& chapterId);
    Q_INVOKABLE void saveChapter(const QString& chapterId, const QString& content);
    Q_INVOKABLE void refreshProject();

    // ── 列表 Model 暴露给 QML（通过 context property 注册）──
    QAbstractListModel* chapterModel() const;
    QAbstractListModel* characterModel() const;

signals:
    // ── 推送（C++ → QML）──
    void tokenReceived(const QString& delta);           // 流式输出逐字追加
    void reasoningReceived(const QString& delta);       // 推理过程（DeepSeek thinking）
    void responseComplete(const QString& fullText);     // 回复完成
    void chapterLoaded(const QString& chapterId, const QString& content);
    void projectChanged();
    void statusChanged(const QString& text);

private:
    agent::Agent& agent_;
    std::shared_ptr<Project> project_;
    QAbstractListModel* chapterModel_;
    QAbstractListModel* characterModel_;
};
```

### QML 侧接线

```qml
// QML 使用示例
import QtQuick 2.15
import QtQuick.Controls 2.15

ApplicationWindow {
    // 列表 — 通过 Model 驱动
    ListView {
        model: bridge.chapterModel()
        delegate: Text { text: model.title }
    }

    // 按钮触发 — 调用 bridge 槽
    Button {
        onClicked: bridge.sendMessage(input.text)
    }

    // 流式接收 — 监听信号
    Connections {
        target: bridge
        function onTokenReceived(delta) { /* 追加到当前 AI 回复 */ }
    }
}
```

---

## 小说文本显示（聊天区流式输出）

AI 回复逐 token 追加显示，使用 `ListView` + `Text` delegate：

```qml
ListView {
    id: chatView
    model: chatModel  // ListModel
    delegate: ColumnLayout {
        width: parent.width
        spacing: 4
        // 角色标签
        Label {
            text: model.role === "user" ? "你" : "墨染"
            font.pixelSize: 12
            color: model.role === "user" ? "#6366f1" : "#10b981"
        }
        // 消息正文
        Text {
            Layout.fillWidth: true
            text: model.content
            font { family: "Noto Serif SC"; pixelSize: 15 }
            lineHeight: 1.8
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            color: "#1f2937"
        }
    }
    // 新消息淡入动画
    add: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 300 }
    }
    // 自动滚到底部
    onContentHeightChanged: {
        if (contentHeight > height)
            contentY = contentHeight - height
    }
}

// 流式追加：AI 回复逐 token 追加到最后一条消息
Connections {
    target: bridge
    function onTokenReceived(delta) {
        var last = chatModel.get(chatModel.count - 1)
        if (last && last.role === "assistant") {
            last.content += delta
            chatModel.set(chatModel.count - 1, last)
        }
    }
}
```

---

## 小说文本编辑（章节编辑器）

章节正文编辑使用 `TextArea` + `Flickable`，模拟书本排版效果。

### 编辑器布局

```qml
// ChapterEditor.qml
ColumnLayout {
    spacing: 0

    // 工具栏
    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 8
        ToolButton { icon.name: "format-bold" }
        ToolButton { icon.name: "format-italic" }
        Item { Layout.fillWidth: true }
        Label { id: wordCount; text: "字数: 0"; font.pixelSize: 12; color: "#6b7280" }
        Button { text: "保存"; onClicked: bridge.saveChapter(currentChapterId, editor.text) }
    }

    // 编辑器主体
    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        color: "white"
        border.color: "#e5e7eb"
        Flickable {
            anchors.fill: parent
            anchors.margins: 40   // 左右留白，模拟书本效果
            contentWidth: editor.width
            contentHeight: editor.height
            clip: true

            TextArea {
                id: editor
                width: flick.width - 80
                font { family: "Noto Serif SC"; pixelSize: 16 }
                lineHeight: 1.9
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                placeholderText: "开始写作..."
                padding: 0
                onTextChanged: wordCount.text = "字数: " + text.length
            }
        }
    }
}
```

### 中文小说排版关键点

| 需求 | QML 实现 |
|------|---------|
| 衬线字体 | `font.family: "Noto Serif SC"` |
| 行高 1.8-2.0 | `lineHeight: 1.9` |
| 左右留白（书页效果） | `Flickable` 的 `anchors.margins: 40` |
| 字数实时统计 | `onTextChanged: wordCount.text = text.length` |
| Markdown 预览 | `Text { textFormat: Text.Markdown }` |

### Markdown 渲染（预览模式）

如果需要在右侧预览渲染后的效果，使用 `Text` 的内置 Markdown 支持：

```qml
Flickable {
    Text {
        text: markdownSource    // 来自编辑器的 Markdown 源码
        textFormat: Text.Markdown
        font { family: "Noto Serif SC"; pixelSize: 16 }
        lineHeight: 1.9
        width: parent.width
        wrapMode: Text.Wrap
        onLinkActivated: Qt.openUrlExternally(link)
    }
}
```

QML 内置 `Text.Markdown` 支持：`#` 标题、`**加粗**`、`*斜体*`、`~~删除线~~`、`> 引用`、`- 列表`、`[链接](url)`。

---

## 验证方法

1. **编译**：`cmake -B build && cmake --build build`，确认无编译错误
2. **CLI 模式**：`novelagent --cli -p <project>` 进入 REPL，确认原有功能正常
3. **QML 模式无项目**：`novelagent` 启动空主窗口，项目树显示空提示
4. **QML 模式有项目**：`novelagent -p <project>` 打开项目，项目树正确显示章节/角色/设定
5. **Agent 对话**：输入消息，确认流式输出逐 token 显示在聊天区，带淡入动画
6. **章节编辑**：点击章节 → 内容加载到编辑器 → 修改后保存
7. **主题一致性**：所有页面使用 Theme.qml 中定义的颜色/字体

全量编译 + 24 个现有测试通过。
