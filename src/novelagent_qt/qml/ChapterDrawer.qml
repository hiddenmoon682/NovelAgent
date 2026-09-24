pragma ComponentBehavior: Bound
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
// 注：委托内经附加属性 ListView.view 访问视图属性，在 popup 关闭/重开生命周期中
// 可能取到 null（实测 279 行 TypeError）——本项目先例已证附加视图"不可靠"；
// 同文件 id（root/listView）访问恒非 null，qmllint 在 ComponentBehavior: Bound 下放行。
// - 键盘与焦点：QML 焦点不自动归还（Keyboard Focus 文档原文）→ onClosed 显式
//   归还（由 Task 5 接线实现）；Esc 关闭需 popup 持焦（Popup Back/Escape 原文）→ focus:true +
//   搜索框 forceActiveFocus；搜索框持焦点会吞掉列表 ↑↓（ScrollBar 也不过滤按键，
//   官方原文）→ 官方同款解法 Keys.onUpPressed/DownPressed 转发（本文件在 Task 4 实现）
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

    // 展示模型随入参同步重建：标题栏的"第 N 章"（currentNum）与抽屉是否打开无关，
    // 必须在 chapters 变化时就绪，不能只等 onOpened（onOpened 仍会再建一次，幂等）。
    onChaptersChanged: _buildOrdered()
    Component.onCompleted: _buildOrdered()

    // 当前章节的全局序号文案（"第 N 章"；当前章不在列表中时为空串）。
    // 标题栏（ReaderPanel.headerTitle）与抽屉行内前缀、页脚"当前 · 第 x 章"共用此编号，
    // 保证同一章在两处显示同一个序号（排序只在 _buildOrdered 一处定义）。
    readonly property string currentNum: root._currentNum()

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
                volume: it.volumeOrder >= 0 ? it.volumeTitle
                       : (root._grouped ? "未分卷" : ""), num: it.num
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
                    font.family: Theme.fontIcon
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
                width: root.width
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
                width: root.width
                height: 36
                radius: Theme.radiusSm
                // 两个"选中"状态分开派生（委托无状态，官方规则）：
                //   isCurrent —— 已载入正文的那一章（currentChapterId）：朱砂半透明底 +「当前」徽标
                //   isCursor  —— 键盘光标行（currentIndex）：只出朱砂标条
                // 分开的原因：抽屉内 ↑↓ 只移动光标、Enter 才载入章节（onAccepted → _pick），
                // 若「当前」徽标也绑光标行，↑↓ 途中会显示"当前"而正文并未切换，属撒谎。
                readonly property bool isCurrent: chapterRow.model.cid === root.currentChapterId
                readonly property bool isCursor: chapterRow.index === listView.currentIndex
                color: chapterRow.isCurrent ? Theme.accentTint
                     : (rowMa.containsMouse ? Theme.bgHover : "transparent")
                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                // 朱砂选中标条（项目选中态规范）：跟随键盘光标行
                Rectangle {
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    width: Theme.markBar
                    height: 16
                    radius: 2
                    color: Theme.accent
                    visible: chapterRow.isCursor
                }

                RowLayout {
                    anchors {
                        left: parent.left; right: parent.right
                        leftMargin: Theme.gapLg + Theme.gapXs; rightMargin: Theme.gapMd
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: Theme.gapSm

                    // 行文案「第 N 章 · 标题」：序号 num 由 _buildOrdered 统一生成，
                    // 与标题栏、页脚同源（预览稿行内即此格式）
                    Label {
                        text: chapterRow.model.num + " · " + chapterRow.model.title
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
                    // 「当前」徽标：仅当前已载入章显示（不可见时布局自动跳过，不占宽度）
                    Rectangle {
                        Layout.preferredWidth: badgeText.implicitWidth + Theme.gapCozy
                        Layout.preferredHeight: 16
                        Layout.alignment: Qt.AlignVCenter
                        radius: Theme.radiusXs
                        color: Theme.accent
                        visible: chapterRow.isCurrent
                        Label {
                            id: badgeText
                            anchors.centerIn: parent
                            text: "当前"
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.sizeMini
                            color: Theme.textPrimary
                        }
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
