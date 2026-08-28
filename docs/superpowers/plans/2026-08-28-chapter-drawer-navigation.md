# 目录抽屉（方案 B）实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development（推荐）或 executing-plans 逐任务执行本计划。步骤用 `- [ ]` 复选框跟踪。
>
> 状态：**等待用户确认**（确认前不执行任何任务）。

**Goal:** 把阅读面板的章节选择从「300px 滚动弹窗」升级为「面板内目录抽屉」：搜索 + 卷分组 + 当前章节定位 + 「共 N 章 · 当前 · 第 x 章」页脚，可支撑数百章节的导航。

**Architecture:** 抽屉 = `Popup(parent: 阅读面板, modal:false)` + 面板内自绘遮罩（官方 Popup 坐标相对 parent、内容挂窗口 overlay 保证最前；modal 遮罩是窗口级故自绘）。分组用官方 `ListView.section`（ViewSection）机制，模型在 QML 侧按（卷序, 章序）排序后构建；章节列表数据由 `QmlBridge::chapterList()` 补带卷字段（`volumeId/volumeTitle/volumeOrder`）后经属性注入抽屉组件，抽屉不直接访问 bridge。开合动画用官方 `enter/exit` Transition（220ms 滑入滑出）。

**Tech Stack:** Qt 6.8.3 · QML（QtQuick + QtQuick.Controls）· C++20（仅 QmlBridge 一处小改）· CMake/Ninja · MSYS2 MinGW-w64。

**Spec / 设计依据：**
- 交互定稿：`docs/design/previews/chapter-nav-preview.html`（方案 B 最终形态：右侧滑入抽屉 + 变暗遮罩 + 卷分组可折叠区 + 搜索 + 页脚「共 N 章 | 当前 · 第 x 章」；经用户三轮反馈确认：① 选方案 B；② 底部改为方案 C 弹出层同款页脚）。
- Qt 官方文档核验报告（三份子代理研究，均已核对 doc.qt.io/qt-6.8 原文）：
  1. ListView 分组/定位/委托/滚动条：`qml-qtquick-listview.html`（section 组、positionViewAtIndex、「Avoid Storing State in Delegates」、「Variable Delegate Size and Section Labels」）、`qml-qtquick-controls-scrollbar.html`、`qml-qtquick-flickable.html`、`qtquick-modelviewsdata-modelview.html`、`qtquick-performance.html`；
  2. 抽屉形态/弹层/焦点/动画：`qml-qtquick-controls-popup.html`、`qml-qtquick-controls-drawer.html`、`qml-qtquick-controls-overlay.html`、`qml-qtquick-numberanimation.html`、`qml-qtquick-behavior.html`、`qml-qtquick-statesanimations-animations.html`、`qtquick-input-focus.html`、`qml-qtquick-layouts-layout.html`、`qtquicklayouts-overview.html`；
  3. 仓库取证：QmlBridge.cpp:649-674（chapterList 现状）、ReaderPanel.qml 全文、Theme.qml、ThemedField.qml、ModalDimmer 等 12 文件。
- 关键机制结论（实现前已固化，任务注释中引用原文）：
  - **形态选 Popup 而非 Drawer**：Drawer 是 Popup 子类，默认窗口坐标 + modal 遮罩窗口级 + 开合动画时长无文档化属性；Popup 的 x/y 相对 parent、contentItem 自动挂窗口 overlay、enter/exit 是官方 Transition（原文出处见 Task 3 注释）。
  - **定位**：`positionViewAtIndex(int, PositionMode)` 官方唯一签名（无 ScrollMode 重载）；官方时机「only be called after the Component has completed」；Popup 等效时机 = `onOpened`（官方 opened 语义：visible 且 enter/exit 过渡均结束）；禁 contentY（官方明确不推荐）。
  - **分组**：`section.property/criteria(FullString)/delegate`，模型必须预排序（分组不排序，官方原文）；分区头官方模式 `required property string section` + `ListView.view.width`，分区头不绑定模型行。
  - **键盘**：搜索框持焦点会吞 ↑↓（ListView 静默不响应、ScrollBar 不过滤按键），官方解法 `Keys.onUpPressed/DownPressed` 转发 `decrement/incrementCurrentIndex`；中文输入法组合期间（preeditText 非空）不转发。
  - **焦点**：QML 焦点不自动归还（官方原文）→ `onClosed` 显式归还；Popup 持焦（`focus: true`）Esc 才生效（官方 Back/Escape 原文）。
  - **遮罩**：窗口级 Overlay 无法只盖面板 → 面板内自绘 Rectangle（`Theme.overlayDim`）。
  - **ScrollBar 估算**：章节行等高（官方唯一推荐之一）；section 头拉长所属首行 → 估算轻微跳动可接受。
  - **委托**：`required property`（index/model）；状态从 `ListView.isCurrentItem`/`view.currentIndex` 派生，禁存委托（官方原文）。

## Global Constraints

- **注释/提交信息/CHANGELOG 一律中文**；代码标识符英文。
- **Theme.qml 单一事实来源**：颜色/字号/间距/圆角/尺寸档位禁止内联魔法值——新尺寸（抽屉宽度）必须先补 Theme 档位（Task 1）。
- **布局优先**：RowLayout/ColumnLayout 优先；布局容器子项禁写 anchors/几何绑定（官方定性 undefined behavior + 运行时警告）；布局外装饰/强制坐标场景的 anchors 必须带注释。
- **布局内 Rectangle 必须显式尺寸**（implicitWidth/Height 为 0 会塌陷叠字）。
- **委托规范**：`required property`；无状态；取宽用 `ListView.view.width`（官方模式，禁 `parent.width`——官方点名 "Incorrect."）。
- **QML 警告零容忍**：运行日志不得出现 `QQmlEngine::warnings`；qmllint 告警分档（布局类 3 条属项目规范留白 + bridge 上下文属性 2 条既有一致，新增文件须 0 新增告警）。
- **验证门**：`./scripts/verify.sh`（全量回归，走 MSYS2 bash）；GUI 双预设构建（default + release）；声称完成必须附命令输出。
- **qt-debug-verify 第 0 步**：实现中若发现与计划假设不符的行为，先回查 doc.qt.io/qt-6.8（注意 Qt 6.8 起 Controls 类型页 URL 无 "2" 前缀），再动手，禁止试错式连改。
- **提交纪律**：工作区已有大批未提交改动（工具卡片/去 markdown/嵌入等工作）；每个任务 `git add` **只加本任务涉及文件**，不得打包既有批次。
- **数据流规则**：QML 不直接访问 C++；抽屉组件只经属性/信号与 ReaderPanel 通信。
- 中文路径 I/O 已由既有代码处理（本计划不改文件路径逻辑）。

## 文件结构

| 文件 | 责任 | 动作 |
|---|---|---|
| `src/novelagent_qt/qml/Theme.qml` | 新增抽屉宽度档位 `readerDrawerWidth` | 修改 |
| `src/novelagent_qt/QmlBridge.cpp` | `chapterList()` 每章补带 `volumeId/volumeTitle/volumeOrder` | 修改 |
| `src/novelagent_qt/qml/ChapterDrawer.qml` | 新组件：目录抽屉（搜索/分组/定位/页脚/键盘） | 新建 |
| `src/novelagent_qt/qml/ReaderPanel.qml` | 标题栏改 chip+☰、删旧弹窗、挂抽屉与遮罩、焦点归还 | 修改 |
| `tests/probe_drawer.qml` | 一次性探针（tests/ 目录 gitignored，不提交） | 新建（临时） |
| `CHANGELOG.md` | 变更记录（最新在上，Task 6） | 修改 |
| `docs/superpowers/plans/2026-08-28-chapter-drawer-navigation.md` | 本计划 | 已建 |

---

## Task 1: Theme 抽屉宽度档位

**Files:**
- Modify: `src/novelagent_qt/qml/Theme.qml`（阅读区段距之后）

**Interfaces:**
- Produces: `Theme.readerDrawerWidth: int`（= 300）——Task 3 的 Popup 宽度上限；后续任务依赖此名，不得改名。

- [ ] **Step 1: 编辑 Theme.qml**

在 `readerParagraphGap` 行之后新增（注释中文）：

```qml
    // ── 阅读区尺寸 ──
    readonly property int readerDrawerWidth: 300  // 目录抽屉宽度（方案 B；尺寸档位，禁内联魔法值）
```

- [ ] **Step 2: 机械验证**

```bash
D:/QT/QT/6.8.3/mingw_64/bin/qmllint.exe -I src/novelagent_qt/qml src/novelagent_qt/qml/Theme.qml
```
预期：无输出（0 告警）。

- [ ] **Step 3: 提交**

```bash
git add src/novelagent_qt/qml/Theme.qml
git commit -m "feat(qml): Theme 新增 readerDrawerWidth 档位（目录抽屉宽度）"
```

---

## Task 2: QmlBridge::chapterList 补带卷字段

**Files:**
- Modify: `src/novelagent_qt/QmlBridge.cpp:649-674`（chapterList 函数体）
- 头部 include 视需要补：`#include <QHash>`、`#include <utility>`（若文件已有可跳过）

**Interfaces:**
- Consumes: `Project::outline.volumes`（`std::vector<Volume>`，Volume.id/title/order）、`Chapter::volume_id`（string，可空）。
- Produces: `chapterList()` 每个元素新增三个字段（类型：`QString volumeId`、`QString volumeTitle`、`int volumeOrder`，无卷或引用失效时分别为空串/空串/-1）。现有 `id/title/order/wordCount` 语义不变。Task 3 依赖此契约。

- [ ] **Step 1: 改写 chapterList()**

保持锁内读取与按 `order` 排序现状，在循环前建卷索引、循环内补三个字段：

```cpp
QVariantList QmlBridge::chapterList() const {
    QVariantList list;
    if (!app_ || !app_->projectAccess()) return list;

    // 锁内读取章节元数据（GUI 线程与池线程工具并发，防撕裂读）
    app_->projectAccess()->withReadLock([&](const Project& p) {
        // 卷索引（id → {标题, 卷序}）：供目录抽屉按卷分组；卷序取 volumes 数组下标
        QHash<QString, std::pair<QString, int>> volMeta;
        for (int i = 0; i < int(p.outline.volumes.size()); ++i) {
            const Volume& v = p.outline.volumes[i];
            volMeta.insert(QString::fromStdString(v.id),
                           {QString::fromStdString(v.title), i});
        }

        std::vector<const Chapter*> sorted;
        sorted.reserve(p.outline.chapters.size());
        for (const auto& ch : p.outline.chapters)
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
            // 卷信息：无卷或卷引用失效（volumes 中找不到）时 volumeOrder=-1，
            // QML 端据此把该章归入"未分卷"并排在末尾
            QString volId, volTitle;
            int volOrder = -1;
            const auto it = volMeta.constFind(QString::fromStdString(ch->volume_id));
            if (it != volMeta.constEnd()) {
                volId = it.key();
                volTitle = it.value().first;
                volOrder = it.value().second;
            }
            m.insert(QStringLiteral("volumeId"), volId);
            m.insert(QStringLiteral("volumeTitle"), volTitle);
            m.insert(QStringLiteral("volumeOrder"), volOrder);
            list.push_back(m);
        }
    });
    return list;
}
```

- [ ] **Step 2: 构建验证**

```bash
cd /d/C++Code/C++NovelAgent
D:/SoftWare/msys2/usr/bin/bash.exe -lc './scripts/verify.sh --build'
```
预期：构建成功（退出码 0，输出尾部 BUILD SUCCESS 类字样；若 QmlBridge.cpp 改动触发 PCH 相关重编属正常）。

- [ ] **Step 3: 提交**

```bash
git add src/novelagent_qt/QmlBridge.cpp
git commit -m "feat(qt): chapterList 补带卷字段（volumeId/volumeTitle/volumeOrder），支撑目录抽屉分组"
```

---

## Task 3: ChapterDrawer.qml 骨架（结构 / 展示模型 / 分组 / 页脚 / 定位 / 开合）

**Files:**
- Create: `src/novelagent_qt/qml/ChapterDrawer.qml`
- Test(探针, 不提交): `tests/probe_drawer.qml`

**Interfaces:**
- Consumes: `Theme.readerDrawerWidth`；`Theme` 全档位；`ThemedField` 组件；属性 `chapters`（Task 2 契约数组）、`currentChapterId`（string，空=无选中）。
- Produces: 组件 `ChapterDrawer`——属性 `chapters: var`、`currentChapterId: string`；信号 `chapterSelected(string chapterId)`；打开时自动重建全量模型 + 定位当前章节 + 聚焦搜索框。Task 5 依赖上述接口；本任务内置函数 `_buildOrdered/_rowOf/_currentNum/_refreshModel/_pick`（Task 4 复用 `_refreshModel`）。

> 本任务代码注释内已固化官方依据（doc.qt.io/qt-6.8），实现偏差回查原文时以注释中的 URL 为准（Qt 6.8 起 Controls URL 无 "2" 前缀）。

- [ ] **Step 1: 创建 ChapterDrawer.qml（完整代码如下）**

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ChapterDrawer — 目录抽屉（方案 B，预览 docs/design/previews/chapter-nav-preview.html）。
//
// 官方依据（doc.qt.io/qt-6.8 原文已核）：
// - 形态选 Popup 而非 Drawer：Popup 文档 "x and y coordinates are relative to its
//   parent"，contentItem 自动挂窗口 overlay 保证场景最前（"the content item is
//   automatically reparented to the overlay item"）；而 modal 遮罩是窗口级
//   （Overlay 文档 "The overlay is an ordinary Item that covers the entire window"），
//   Overlay.modal/modeless 只能挂 Popup —— 故本组件 modal:false，面板内遮罩由
//   ReaderPanel 自绘 Rectangle（Theme.overlayDim）。Drawer 控件（Popup 子类，
//   默认窗口坐标）开合动画时长无文档化属性，弃用。
// - 开合动画：Popup 官方 enter/exit Transition（官方示例即 NumberAnimation on
//   opacity），顶层动画并行；时长 Theme.animNormal=220ms。
// - 定位：positionViewAtIndex(int, PositionMode) 是官方唯一签名（无 ScrollMode
//   重载）；官方明示 "methods should only be called after the Component has
//   completed"；Popup 的 opened 语义 = "visible and neither the enter nor exit
//   transitions are running"（等价于已完成且尺寸已定），故定位放 onOpened；
//   禁 contentY（ListView 文档 "It is not recommended to use contentX or
//   contentY..."）。
// - 分组：ListView.section 三件套；官方明示分组是纯视觉层，"Adding sections ...
//   does not automatically re-order the list items"，模型必须按（卷序, 章序）
//   预排序；分区头官方模式 required property string section + ListView.view.width
//   （分区头不绑定模型行，勿写模型角色）。
// - 委托：官方 required property 模式；状态从 ListView.isCurrentItem /
//   ListView.view.currentIndex 派生，不存委托（"State should never be stored in
//   a delegate"）；章节行等高 36px（"It is recommended to have equally-sized
//   delegates"，ScrollBar 估算稳定的官方推荐）。
// - 键盘与焦点：QML 焦点不自动归还（Keyboard Focus 文档原文）→ onClosed 显式
//   归还；Esc 关闭需 popup 持焦（Popup Back/Escape 原文）→ focus:true +
//   搜索框 forceActiveFocus；搜索框持焦点会吞掉列表 ↑↓（ScrollBar 也不过滤按键，
//   官方原文）→ 官方同款解法 Keys.onUpPressed/DownPressed 转发
//   increment/decrementCurrentIndex；输入法组合期间（preeditText 非空）不转发。
Popup {
    id: root
    modal: false
    padding: 0
    // 点击抽屉外（含面板遮罩）与 Esc 关闭；与预览"点灰色区域/Esc 关闭"一致
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    focus: true   // 持焦使 Esc 关闭生效（官方 Back/Escape 处理前提）

    // ── 对外接口 ──
    // 章节数组（ReaderPanel 经 bridge.chapterList() 提供）：
    //   id/title/wordCount/order/volumeId/volumeTitle/volumeOrder
    property var chapters: []
    // 当前选中章节 id（空 = 无选中；由 ReaderPanel 保持）
    property string currentChapterId: ""
    // 用户点选章节（携带 id，ReaderPanel 负责加载与关闭）
    signal chapterSelected(string chapterId)

    // ── 内部数据 ──
    // 展示模型：按（卷序, 章序, id）排序，行内带全局序号 num
    property var _ordered: []
    // 卷数 >= 2 才显示分区头（单卷/无卷时平铺，避免"未分卷"头突兀）
    property bool _grouped: false

    // 排序 + 分组判定（无卷章 volumeOrder=-1 → 排末尾，与页脚序号一致）
    function _buildOrdered() {
        var arr = []
        for (var i = 0; i < root.chapters.length; ++i) arr.push(root.chapters[i])
        arr.sort(function(a, b) {
            var va = a.volumeOrder >= 0 ? a.volumeOrder : 999999
            var vb = b.volumeOrder >= 0 ? b.volumeOrder : 999999
            if (va !== vb) return va - vb
            if (a.order !== b.order) return a.order - b.order
            return a.id < b.id ? -1 : (a.id > b.id ? 1 : 0)
        })
        for (var k = 0; k < arr.length; ++k) arr[k].num = "第 " + (k + 1) + " 章"
        var vols = {}
        for (var j = 0; j < arr.length; ++j) {
            var key = arr[j].volumeOrder >= 0 ? arr[j].volumeId : ""
            if (key !== "") vols[key] = true
        }
        _grouped = Object.keys(vols).length >= 2
        _ordered = arr
    }

    // 当前章节在展示模型中的行号（-1 = 不存在）
    function _rowOf(id) {
        for (var i = 0; i < _ordered.length; ++i)
            if (_ordered[i].id === id) return i
        return -1
    }

    // 页脚"当前 · 第 x 章"文案（按展示模型全局位置；order 为卷内序号，不能直用）
    function _currentNum() {
        var r = _rowOf(root.currentChapterId)
        return r >= 0 ? _ordered[r].num : ""
    }

    // 重建列表模型：q 为空 = 全量；命中 = 标题包含 q（忽略大小写）或全局序号
    // 包含 q（如 "12" 命中 第 12 章）。重建后重算当前章节映射（不在结果中 = -1）
    function _refreshModel(q) {
        q = (q === undefined ? "" : q).trim().toLowerCase()
        listModel.clear()
        for (var i = 0; i < _ordered.length; ++i) {
            var it = _ordered[i]
            var hit = q.length === 0
                || it.title.toLowerCase().indexOf(q) >= 0
                || it.num.indexOf(q) >= 0
            if (!hit) continue
            listModel.append({
                cid: it.id, title: it.title, words: it.wordCount,
                volume: it.volumeOrder >= 0 ? it.volumeTitle : "", num: it.num
            })
        }
        var newIdx = -1
        for (var k = 0; k < listModel.count; ++k)
            if (listModel.get(k).cid === root.currentChapterId) { newIdx = k; break }
        listView.currentIndex = newIdx
        emptyHint.visible = listModel.count === 0
    }

    // 打开前准备：清搜索 → 排序重建 → 定位当前章节 → 搜索框聚焦
    onOpened: {
        searchField.text = ""
        _buildOrdered()
        _refreshModel("")
        if (listView.currentIndex >= 0)
            listView.positionViewAtIndex(listView.currentIndex, ListView.Contain)
        searchField.forceActiveFocus()
    }

    // 选章：回调 ReaderPanel 并关闭
    function _pick(rowIndex) {
        var cid = listModel.get(rowIndex).cid
        root.chapterSelected(cid)
        root.close()
    }

    // 只读视图状态（供外部只读联动与探针断言；委托状态仍一律走视图派生）
    readonly property int viewCurrentIndex: listView.currentIndex
    readonly property int viewCount: listView.count

    // ── 几何：相对 parent（阅读面板）；x 由过渡驱动，不绑 x（防绑定回弹） ──
    width: Math.min(Theme.readerDrawerWidth, root.parent !== null ? root.parent.width : Theme.readerDrawerWidth)
    height: root.parent !== null ? root.parent.height : 400
    y: 0
    x: 0   // 占位初始值，实际由 enter/exit 过渡从"面板右缘外"滑入

    // 背景：面板同底色 + 左缘分割线（右侧贴面板边缘）
    background: Rectangle {
        color: Theme.bgElevated
        Rectangle {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: 1
            color: Theme.divider
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        // 头部：目录 + 关闭
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: "transparent"

            Label {
                anchors { left: parent.left; leftMargin: Theme.gapLg; verticalCenter: parent.verticalCenter }
                text: "目录"
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.sizeTitle
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
            Rectangle {
                id: closeBtn
                anchors { right: parent.right; rightMargin: Theme.gapSm; verticalCenter: parent.verticalCenter }
                width: 32
                height: 32
                radius: Theme.radiusSm
                color: closeMa.containsMouse ? Theme.bgHover : "transparent"
                Behavior on color { ColorAnimation { duration: Theme.animFast } }
                Label {
                    anchors.centerIn: parent
                    text: "\uE8BB"   // Segoe MDL2 关闭
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeUi
                    color: Theme.textSecondary
                }
                MouseArea {
                    id: closeMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.close()
                }
            }
        }

        // 搜索框：ThemedField 复用（Task 4 挂过滤与键盘）
        ThemedField {
            id: searchField
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapLg
            Layout.rightMargin: Theme.gapLg
            Layout.bottomMargin: Theme.gapSm
            placeholder: "搜索章节（支持章节号/标题）"
        }

        // 章节列表
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: listModel
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            // 官方 ViewSection 分组（模型已按卷序/章序排好；仅多卷时启用）
            section.property: root._grouped ? "volume" : ""
            section.criteria: ViewSection.FullString
            section.delegate: Rectangle {
                id: secRow
                required property string section
                width: ListView.view.width
                height: 30
                color: Theme.bgElevated
                Rectangle {
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    height: 1
                    color: Theme.divider
                }
                Label {
                    anchors { left: parent.left; leftMargin: Theme.gapMd; verticalCenter: parent.verticalCenter }
                    text: secRow.section
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeNote
                    font.weight: Font.DemiBold
                    color: Theme.textSecondary
                }
            }

            delegate: Rectangle {
                id: chapterRow
                required property var model
                required property int index
                width: ListView.view.width
                height: 36
                radius: Theme.radiusSm
                // 选中/悬停均派生自视图状态（委托无状态，官方规则）
                color: (index === ListView.view.currentIndex || rowMa.containsMouse)
                       ? Theme.bgHover : "transparent"
                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                // 朱砂选中标条（项目选中态规范）
                Rectangle {
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    width: Theme.markBar
                    height: 16
                    radius: 2
                    color: Theme.accent
                    visible: index === ListView.view.currentIndex
                }

                RowLayout {
                    anchors {
                        left: parent.left; right: parent.right
                        leftMargin: Theme.gapLg + Theme.gapXs; rightMargin: Theme.gapMd
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: Theme.gapSm

                    Label {
                        text: chapterRow.model.title
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.sizeUi
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: chapterRow.model.words > 0
                        text: chapterRow.model.words + " 字"
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.sizeCaption
                        color: Theme.textFaint
                    }
                }

                MouseArea {
                    id: rowMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root._pick(chapterRow.index)
                }
            }

            // 空态（无章节 / 搜索无匹配）
            Label {
                id: emptyHint
                anchors.centerIn: parent
                text: root.chapters.length === 0 ? "暂无章节" : "无匹配章节 · 换个关键词试试"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeUi
                color: Theme.textFaint
            }
        }

        // 页脚：与方案 C 弹出层同款（左"共 N 章" 右朱砂"当前 · 第 x 章"）
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: "transparent"

            Rectangle {
                anchors { top: parent.top; left: parent.left; right: parent.right }
                height: 1
                color: Theme.divider
            }
            Label {
                anchors { left: parent.left; leftMargin: Theme.gapLg; verticalCenter: parent.verticalCenter }
                text: "共 " + root.chapters.length + " 章"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textFaint
            }
            Label {
                anchors { right: parent.right; rightMargin: Theme.gapLg; verticalCenter: parent.verticalCenter }
                text: "当前 · " + root._currentNum()
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                font.weight: Font.DemiBold
                color: Theme.accent
                visible: root._currentNum() !== ""
            }
        }
    }

    // ── 开合动画：官方 enter/exit Transition，220ms 平行滑入/滑出 ──
    enter: Transition {
        NumberAnimation {
            property: "x"
            from: root.parent !== null ? root.parent.width : 0
            to: root.parent !== null ? root.parent.width - root.width : 0
            duration: Theme.animNormal
            easing.type: Easing.OutCubic
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "x"
            to: root.parent !== null ? root.parent.width : 0
            duration: Theme.animNormal
            easing.type: Easing.InCubic
        }
    }

    ListModel { id: listModel }
}
```

- [ ] **Step 2: 建探针 tests/probe_drawer.qml（临时，不提交）**

```qml
import QtQuick
import QtQuick.Controls
import "../src/novelagent_qt/qml"

// 探针：验证 ChapterDrawer 的公开数据层（排序/分组/映射/过滤/选章信号）。
// 组件内部 id 跨文档不可见（QML 作用域规则），故只走公开属性与函数
// （_ordered/_grouped/_rowOf/_currentNum/_refreshModel/_pick + 只读
// viewCurrentIndex/viewCount）；列表渲染与动画路径由用户手动复验。
// 时序：open() 后 onOpened 需等 enter 过渡（220ms）结束才触发
// （官方 opened 语义），故断言排队在打开 700ms 后的第二个 Timer。
// 运行：D:/QT/QT/6.8.3/mingw_64/bin/qml.exe tests/probe_drawer.qml
// 判定：输出包含 "PROBE-PASS" 即通过（qml.exe 不保证退出码，以输出为准）。
Window {
    id: win
    width: 420
    height: 600
    visible: true
    property string picked: ""   // 选章信号捕获

    Rectangle {
        id: panel
        anchors.fill: parent
        color: "#2a251e"

        ChapterDrawer {
            id: drawer
            // 两卷 + 一卷外章节，故意乱序，验证排序与"未分卷"殿后
            chapters: [
                { id: "c3", title: "第三卷章", wordCount: 800, order: 1, volumeId: "v2", volumeTitle: "第二卷", volumeOrder: 1 },
                { id: "c0", title: "雨夜来客", wordCount: 824, order: 1, volumeId: "v1", volumeTitle: "第一卷", volumeOrder: 0 },
                { id: "c2", title: "孤悬章", wordCount: 500, order: 2, volumeId: "", volumeTitle: "", volumeOrder: -1 },
                { id: "c1", title: "第二卷章", wordCount: 600, order: 2, volumeId: "v1", volumeTitle: "第一卷", volumeOrder: 0 }
            ]
            currentChapterId: "c1"
            onChapterSelected: (chapterId) => { win.picked = chapterId }
        }

        Timer {
            id: openTimer
            interval: 300
            repeat: false
            onTriggered: drawer.open()
        }
        Timer {
            id: checkTimer
            interval: 1000   // 300 + 打开动画 220 + 余量
            repeat: false
            onTriggered: {
                // 关闭后的 onOpened 数据层断言（打开动画已完成）
                var ok = drawer._ordered.length === 4
                ok = ok && drawer._ordered[0].id === "c0"
                ok = ok && drawer._ordered[2].id === "c3"
                ok = ok && drawer._ordered[3].id === "c2"      // 未分卷殿后
                ok = ok && drawer._ordered[0].num === "第 1 章"
                ok = ok && drawer._rowOf("c1") === 1
                ok = ok && drawer._currentNum() === "第 2 章"
                ok = ok && drawer._grouped === true
                console.log(ok ? "PROBE-PASS data" : "PROBE-FAIL data")
                // 打开定位：视图 currentIndex 应映射到 c1 的行号 1
                ok = drawer.viewCurrentIndex === 1
                console.log(ok ? "PROBE-PASS open-position" : "PROBE-FAIL open-position")
                // 选章信号：取展示模型第 0 行（c0，排序后首行）
                drawer._pick(0)
                ok = win.picked === "c0"
                console.log(ok ? "PROBE-PASS pick" : "PROBE-FAIL pick")
                Qt.quit()
            }
        }
    }
}
```

- [ ] **Step 3: 跑探针**

```bash
D:/QT/QT/6.8.3/mingw_64/bin/qml.exe -I src/novelagent_qt/qml tests/probe_drawer.qml 2>&1 | grep -E "PROBE-(PASS|FAIL)"
```
预期：`PROBE-PASS data`、`PROBE-PASS open-position`、`PROBE-PASS pick` 三行（无 FAIL）。
> 注意：`import "../src/novelagent_qt/qml"` 目录导入依赖 qmldir/单例机制（Theme 自身 pragma Singleton）；若 qml.exe 报 Theme 解析失败，改用 `-I src/novelagent_qt/qml` 并把首行 import 改为文件相对路径引用——两种通路先按上述方式跑，失败再看输出修正，禁止猜改组件代码。

- [ ] **Step 4: qmllint**

```bash
D:/QT/QT/6.8.3/mingw_64/bin/qmllint.exe -I src/novelagent_qt/qml src/novelagent_qt/qml/ChapterDrawer.qml
```
预期：无输出（0 告警）。若出现告警，逐条对照项目基线（布局类/上下文属性类不新增）处理。

- [ ] **Step 5: 提交**

```bash
git add src/novelagent_qt/qml/ChapterDrawer.qml
git commit -m "feat(qml): 新增 ChapterDrawer 目录抽屉组件（官方 Popup+ViewSection 方案，含排序分组/定位/页脚）"
```

---

## Task 4: 搜索过滤与键盘导航

**Files:**
- Modify: `src/novelagent_qt/qml/ChapterDrawer.qml`（searchField 区块 + 无其它改动）
- Test(临时): `tests/probe_drawer.qml`（追加断言）

**Interfaces:**
- Consumes: Task 3 的 `_refreshModel(q)`、`_pick(rowIndex)`、`listView`。
- Produces: 搜索框输入即时过滤；↑↓ 移动列表选中（IME 组合期不转发）；Enter 选中当前行。Task 5 直接复用。

- [ ] **Step 1: 修改 searchField**

把 Task 3 中的 searchField 区块替换为：

```qml
        // 搜索框：即时过滤（重建 ListModel，官方性能页建议 filtered model 而非
        // visible 隐藏——"the space it occupied in the view will remain"）；
        // 键盘：↑↓ 驱动列表 currentIndex（官方 ScrollBar 示例同款转发模式：
        // ScrollBar 不过滤按键；搜索框持焦时 ListView 自带导航收不到方向键），
        // 输入法组合期间（preeditText 非空）不转发，避免吞候选翻页；
        // Enter 走 TextField 官方 onAccepted 信号选中当前行
        ThemedField {
            id: searchField
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapLg
            Layout.rightMargin: Theme.gapLg
            Layout.bottomMargin: Theme.gapSm
            placeholder: "搜索章节（支持章节号/标题）"
            onTextEdited: root._refreshModel(searchField.text)

            Keys.onUpPressed: {
                if (searchField.preeditText.length > 0) return
                if (listView.currentIndex > 0) listView.decrementCurrentIndex()
            }
            Keys.onDownPressed: {
                if (searchField.preeditText.length > 0) return
                if (listView.currentIndex < listView.count - 1) listView.incrementCurrentIndex()
            }
            onAccepted: {
                if (listView.currentIndex >= 0) root._pick(listView.currentIndex)
            }
        }
```

- [ ] **Step 2: 追加探针断言（tests/probe_drawer.qml，Timer 内 open 之后）**

```qml
                // 搜索过滤（走公开函数 _refreshModel；输入框 onTextEdited 的
                // 一行接线由 qmllint + 手动复验覆盖）
                drawer._refreshModel("雨")
                ok = drawer.viewCount === 1 && drawer.viewCurrentIndex === 0
                console.log(ok ? "PROBE-PASS search" : "PROBE-FAIL search")
                // 数字命中全局序号："第 3 章" 即 c3
                drawer._refreshModel("3")
                ok = drawer.viewCount === 1 && drawer.viewCurrentIndex === 0
                ok = ok && drawer._ordered[2].id === "c3"   // 展示序号仍为全局位次
                console.log(ok ? "PROBE-PASS search-num" : "PROBE-FAIL search-num")
                // 无匹配：count 0 且 currentIndex -1
                drawer._refreshModel("不存在的章节名")
                ok = drawer.viewCount === 0 && drawer.viewCurrentIndex === -1
                console.log(ok ? "PROBE-PASS search-empty" : "PROBE-FAIL search-empty")
                // 恢复全量：currentChapterId（c1）重新映射到行 1
                drawer._refreshModel("")
                ok = drawer.viewCount === 4 && drawer.viewCurrentIndex === 1
                console.log(ok ? "PROBE-PASS search-reset" : "PROBE-FAIL search-reset")
```

- [ ] **Step 3: 重跑探针**

```bash
D:/QT/QT/6.8.3/mingw_64/bin/qml.exe -I src/novelagent_qt/qml tests/probe_drawer.qml 2>&1 | grep -E "PROBE-(PASS|FAIL)"
```
预期：7 行全 PASS（data / open-position / pick / search / search-num / search-empty / search-reset），无 FAIL。

- [ ] **Step 4: qmllint 复检**（同 Task 3 Step 4，预期 0 告警）

- [ ] **Step 5: 提交**

```bash
git add src/novelagent_qt/qml/ChapterDrawer.qml
git commit -m "feat(qml): 目录抽屉搜索过滤与键盘导航（官方 Keys 转发模式，IME 组合期豁免）"
```

---

## Task 5: ReaderPanel 接线（标题栏 chip+☰ / 删旧弹窗 / 遮罩 / 焦点归还）

**Files:**
- Modify: `src/novelagent_qt/qml/ReaderPanel.qml`
  - 删除：`selectChapter` 函数（旧弹窗专用）、`chapterPopup` 及其所有子项（现状约 87-216 行区域）、`selectorBtn` 区块；
  - 新增：抽屉实例、遮罩 Rectangle、按 id 选章函数、焦点归还；
  - 保留不动：`splitParagraphs` / `reload` / `openChapter` / `currentTitle` / `Connections` / 正文区与底部字数区。

**Interfaces:**
- Consumes: `ChapterDrawer` 组件（Task 3/4）、`Theme.overlayDim`。
- Produces: ReaderPanel 行为——标题 chip / ☰ 打开抽屉；`selectChapterById(id)` 供抽屉回调（保持 keepId 语义：设 currentIndex → openChapter 载入正文）；遮罩随抽屉开关显隐；抽屉关闭后焦点归还目录按钮。

- [ ] **Step 1: 替换标题栏区块**

把现状「── 章节选择栏 ──」区块（含 selectorBtn 与 chapterPopup 全部内容）整体替换为：

```qml
        // ── 章节选择栏（方案 B：标题 chip + 目录按钮 → 打开目录抽屉）──
        Rectangle {
            Layout.fillWidth: true
            height: 48
            color: "transparent"

            RowLayout {
                anchors {
                    left: parent.left; leftMargin: Theme.gapLg
                    right: parent.right; rightMargin: Theme.gapSm
                    verticalCenter: parent.verticalCenter
                }
                spacing: Theme.gapSm

                // 章节标题 chip：点击打开目录抽屉（无章节时禁用）
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    radius: Theme.radiusSm
                    color: (chipMa.containsMouse || chapterDrawer.opened) ? Theme.bgHover : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }

                    RowLayout {
                        anchors {
                            left: parent.left; leftMargin: Theme.gapMd
                            right: parent.right; rightMargin: Theme.gapSm
                            verticalCenter: parent.verticalCenter
                        }
                        spacing: Theme.gapSm

                        Label {
                            text: root.currentTitle
                            font.family: Theme.fontDisplay
                            font.pixelSize: Theme.sizeTitle
                            font.weight: Font.DemiBold
                            color: Theme.textPrimary
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: "\u25be"
                            visible: root.chapters.length > 0
                            font.pixelSize: Theme.sizeUi
                            color: Theme.textSecondary
                        }
                    }

                    MouseArea {
                        id: chipMa
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: root.chapters.length > 0
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: chapterDrawer.open()
                    }
                }

                // 目录按钮（☰，Segoe MDL2 \uE700）
                Rectangle {
                    id: tocBtn
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    radius: Theme.radiusSm
                    visible: root.chapters.length > 0
                    color: (tocMa.containsMouse || chapterDrawer.opened) ? Theme.bgHover : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }

                    Label {
                        anchors.centerIn: parent
                        text: "\uE700"
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.sizeUi
                        color: Theme.textSecondary
                    }
                    ToolTip.visible: tocMa.containsMouse
                    ToolTip.text: "目录"
                    ToolTip.delay: 300

                    MouseArea {
                        id: tocMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: chapterDrawer.open()
                    }
                }
            }
        }
```

- [ ] **Step 2: 替换选章函数**

把 `selectChapter(i)` 函数替换为（保持 keepId 语义与 reload 交互一致）：

```qml
    // 按 id 选章（目录抽屉回调）：设选中并载入正文；id 不存在则忽略
    function selectChapterById(id) {
        for (var i = 0; i < chapters.length; ++i) {
            if (chapters[i].id === id) {
                currentIndex = i
                openChapter(id)
                return
            }
        }
    }
```

- [ ] **Step 3: 挂抽屉实例与遮罩**

在 `ColumnLayout` 之后（root 的直接子级，文档序在后即绘制在上；Popup 不得放进布局容器——官方布局接管子项几何、undefined behavior）追加：

```qml
    // 抽屉实例：parent 缺省即 root（Popup 坐标相对 parent，官方文档）；
    // chapters 由 bridge 提供（含卷字段），currentChapterId 单向同步当前选中
    ChapterDrawer {
        id: chapterDrawer
        chapters: root.chapters
        currentChapterId: (root.currentIndex >= 0 && root.currentIndex < root.chapters.length)
                          ? root.chapters[root.currentIndex].id : ""
        onChapterSelected: (id) => root.selectChapterById(id)
        onClosed: {
            // QML 焦点不自动归还（Keyboard Focus 官方原文），显式还给目录按钮
            tocBtn.forceActiveFocus()
        }
    }

    // 抽屉遮罩：只盖阅读面板（官方 Overlay 遮罩为窗口级，故面板内自绘；
    // 叠于面板内容之上、抽屉 Popup 之下——Popup 内容挂窗口 overlay 天然最上）
    Rectangle {
        id: drawerDim
        anchors.fill: parent
        visible: chapterDrawer.opened
        color: Theme.overlayDim
        z: 10

        MouseArea {
            anchors.fill: parent
            onClicked: chapterDrawer.close()
        }
    }
```

> 遮罩用 `visible: chapterDrawer.opened` 同步抽屉开关（退出过渡期间遮罩即刻消失属可接受；若用户反馈突兀，后续可把遮罩 opacity 挂 Behavior 动画，不在本计划范围）。

- [ ] **Step 4: 构建 + qmllint**

```bash
cd /d/C++Code/C++NovelAgent
D:/SoftWare/msys2/usr/bin/bash.exe -lc './scripts/verify.sh --build'
D:/QT/QT/6.8.3/mingw_64/bin/qmllint.exe -I src/novelagent_qt/qml src/novelagent_qt/qml/ReaderPanel.qml
```
预期：构建成功；qmllint 仅剩项目已知基线告警（布局类 3 条 + bridge 上下文属性 2 条 + 委托外层 id 风格债 1 条，逐条确认无**新增**告警条目）。

- [ ] **Step 5: 全量回归**

```bash
cd /d/C++Code/C++NovelAgent
D:/SoftWare/msys2/usr/bin/bash.exe -lc './scripts/verify.sh'
```
预期：全量测试通过（基线 33/33，输出尾部无 FAILED）。

- [ ] **Step 6: release 预设构建（供用户手动运行）**

```bash
cd /d/C++Code/C++NovelAgent
D:/SoftWare/msys2/usr/bin/bash.exe -lc 'cmake --build --preset release --target novelagent_gui'
```
预期：构建成功。若报 `Permission denied`（上一版 GUI 仍在运行），先让用户关闭应用再重跑。

- [ ] **Step 7: 提交**

```bash
git add src/novelagent_qt/qml/ReaderPanel.qml
git commit -m "feat(qml): 阅读面板接入目录抽屉（标题栏 chip+☰、删旧弹窗、自绘遮罩、焦点归还）"
```

---

## Task 6: 文档与收尾

**Files:**
- Modify: `CHANGELOG.md`（顶部新增条目）

- [ ] **Step 1: CHANGELOG 条目（最新在上，格式与现条目一致）**

```markdown
## [2026-08-28] 阅读面板目录抽屉（方案 B）

- 交互升级：标题栏「章节名 + ▾」弹窗 → 「chip + ☰」打开目录抽屉；
  抽屉自面板右缘滑入（Popup + enter/exit 220ms 官方过渡）、面板变暗
  （自绘遮罩：官方 Overlay 遮罩为窗口级，无法只盖面板）、搜索即时过滤、
  卷分组（官方 ListView.section/ViewSection；仅 ≥2 卷时显示分区头）、
  当前章节定位（官方 positionViewAtIndex + onOpened 时机）、
  页脚「共 N 章 | 当前 · 第 x 章」（与方案 C 弹出层同款）、
  ↑↓/Enter/Esc 键盘（官方 Keys 转发模式，IME 组合期豁免）、
  焦点显式归还（官方：QML 焦点不自动归还）。
- 数据：QmlBridge::chapterList 补带 volumeId/volumeTitle/volumeOrder
  （数据模型本就支持卷，QML 侧此前零接入）；分组按真实卷，无卷项目平铺。
- 依据：doc.qt.io/qt-6.8 官方文档核验后落地（三份子代理研究报告），
  组件内注释固化原文出处；Drawer 控件弃用（动画时长无文档化属性 +
  modal 遮罩窗口级）。
- 验证：探针 7 项全 PASS（排序/分组/映射/定位/选章信号/过滤/空态恢复）；qmllint 无新增告警；
  全量回归通过；release 构建通过；界面复验由用户手动执行。
```

- [ ] **Step 2: 提交**

```bash
git add CHANGELOG.md
git commit -m "docs: CHANGELOG 记录目录抽屉（方案 B）落地"
```

- [ ] **Step 3: 跑最终验证门并贴输出**

依次执行并**把输出粘贴进交付说明**（用词即证据）：
`./scripts/verify.sh`（全量回归）、双预设 GUI 构建、探针输出、qmllint 输出。

---

## 验收清单（用户手动，Agent 不得声称通过）

1. 启动应用（release 构建）→ 运行日志**无任何 QML 警告**；
2. 点标题 chip 或 ☰ → 抽屉自右滑入（220ms）、面板变暗、**当前章节自动滚动到可视区**；
3. 页脚左「共 N 章」、右朱砂「当前 · 第 x 章」随选章更新；
4. 搜索框输入（标题关键词 / 章号数字）→ 列表即时过滤；无匹配显示空态文案；
5. 搜索框内 ↑↓ 移动选中、Enter 打开选中章、Esc / 点灰色区域 / ✕ 关闭；
6. 中文输入法拼字期间 ↑↓ 不误触列表（候选翻页正常）；
7. 选章后抽屉关闭、标题栏与正文跳转、遮罩消失；关闭后键盘焦点不回丢（可继续操作应用）；
8. 无卷项目（现状样例）平铺显示、无分区头；有多卷项目显示卷头（验证方式：经 Agent 创建卷后自行确认）；
9. 章节多时滚动流畅（虚拟化由 ListView 原生保证），滚动条无异常跳变；
10. 原有行为不回归：启动自动选中第一章、Agent 新建章节后列表即时刷新。

## 决策记录（审阅时重点看）

- **组卷规则**：QML 端按 bridge 卷字段分组，**无卷项目平铺**（不显示"未分卷"头）；
  预览中的 3 卷示例为演示数据，真实项目需创建卷后才会出现分组头。
- **章节行高**：固定 36px（官方 ScrollBar 估算唯一的轻量推荐）。
- **页脚序号**：按展示模型全局位置（`第 x 章`），与 outline 的 `order`（卷内序号）解耦。
- **弃用 Drawer 控件**、**弃用旧 chapterPopup**：理由见 Task 3 头部注释。
- **键盘 Enter**：用 ThemedField 官方 `onAccepted` 而非 `Keys.onReturnPressed`（TextField 按键消费差异，避免双触发）。

## 参考（已核 doc.qt.io/qt-6.8 URL）

- ListView: https://doc.qt.io/qt-6.8/qml-qtquick-listview.html
- Flickable: https://doc.qt.io/qt-6.8/qml-qtquick-flickable.html
- ScrollBar: https://doc.qt.io/qt-6.8/qml-qtquick-controls-scrollbar.html
- Popup: https://doc.qt.io/qt-6.8/qml-qtquick-controls-popup.html
- Drawer: https://doc.qt.io/qt-6.8/qml-qtquick-controls-drawer.html
- Overlay: https://doc.qt.io/qt-6.8/qml-qtquick-controls-overlay.html
- NumberAnimation/Behavior/Transition: https://doc.qt.io/qt-6.8/qml-qtquick-numberanimation.html · https://doc.qt.io/qt-6.8/qml-qtquick-behavior.html · https://doc.qt.io/qt-6.8/qml-qtquick-transition.html
- 键盘焦点: https://doc.qt.io/qt-6.8/qtquick-input-focus.html
- Layouts（undefined behavior 原文）: https://doc.qt.io/qt-6.8/qtquicklayouts-overview.html · https://doc.qt.io/qt-6.8/qml-qtquick-layouts-layout.html
- Models/委托 required property: https://doc.qt.io/qt-6.8/qtquick-modelviewsdata-modelview.html