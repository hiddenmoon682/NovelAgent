import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ReaderPanel — 右栏：章节选择 + 只读阅读视图。
// 正文纯文本渲染（小说正文非 Markdown），衬线字体，行高 Theme.lineHeightBody，左右留白书页效果。
Rectangle {
    id: root
    color: Theme.bgReader

    property var chapters: []
    property int currentIndex: -1
    property string chapterContent: ""
    property var paragraphs: []

    // 正文为纯文本（非 Markdown），按"空行"切成段落数组，每段一个 ListView 委托项。
    // 为什么不用富文本：QML Text 的 RichText 实测不兑现段落边距（margin-bottom 无效、
    // 块间还有约 12px 额外高度），官方文档（Supported HTML Subset）声明的块属性是
    // QTextDocument 层行为，QML Text 渲染不遵守；而"行高=整行盒"使纯文本空行
    // 只能按整行高渲染。ListView 分段是 Qt Quick 长文本阅读的惯用方案：
    // 委托 spacing 精确控制段距（≈ 字形高度而非整行高），大章节还天然虚拟化。
    function splitParagraphs(src) {
        var out = []
        var parts = src.split(/\n\s*\n+/)
        for (var i = 0; i < parts.length; ++i) {
            var p = parts[i].trim()
            if (p.length > 0) out.push(p)
        }
        return out
    }

    readonly property string currentTitle:
        (currentIndex >= 0 && currentIndex < chapters.length)
            ? chapters[currentIndex].title : "暂无章节"

    // 刷新章节列表；保持当前选中（按 id 对齐），选中项被删则回到占位。
    // 无选中且存在章节时自动选中第一章并载入正文：此前 currentIndex 恒为 -1，
    // 标题栏永远显示"暂无章节"占位，Agent 刚创建的章节在右侧面板"看不到"（历史反馈）。
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
        var autoPick = false
        if (idx < 0 && chapters.length > 0) {
            idx = 0
            autoPick = true
        }
        currentIndex = idx
        if (idx >= 0 && autoPick) {
            // 仅"此前无选中"时自动打开：已选中章节的刷新只跟随列表，不打断阅读滚动
            openChapter(chapters[idx].id)
        } else if (idx < 0) {
            chapterContent = ""
            paragraphs = []
        }
    }

    function openChapter(id) {
        chapterContent = bridge.loadChapter(id)
        paragraphs = splitParagraphs(chapterContent)
        chapterList.positionViewAtBeginning()
    }

    function selectChapter(i) {
        currentIndex = i
        openChapter(chapters[i].id)
        chapterPopup.close()
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
                        visible: root.chapters.length > 0
                        font.pixelSize: Theme.sizeUi
                        color: Theme.textSecondary
                    }
                }

                MouseArea {
                    id: selectorMa
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: root.chapters.length > 0
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: chapterPopup.open()
                }

                Popup {
                    id: chapterPopup
                    y: selectorBtn.height + Theme.gapXs
                    width: 300
                    height: Math.min(Math.max(chapterListView.contentHeight, 48) + Theme.gapSm * 2, 360)
                    padding: Theme.gapSm

                    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animNormal } }
                    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animFast } }

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

                        // 面板选中态单向同步给视图：委托内经标准附加属性 ListView.view.currentIndex
                        // 读取（官方文档仅承诺经 ListView.view 读取视图属性；实测自定义成员经
                        // 附加视图调用会命中 null，不可靠，故面板函数仍走外层 id——与项目
                        // 其它面板一致，运行时有效，qmllint 静态告警为已知风格债）
                        currentIndex: root.currentIndex

                        delegate: Rectangle {
                            // 官方委托模式：required property 显式声明角色（含 index），
                            // 宽度与视图状态经 ListView.view 附加属性访问
                            id: chapterRow
                            required property var modelData
                            required property int index
                            width: ListView.view.width
                            height: 36
                            radius: Theme.radiusSm
                            color: (index === ListView.view.currentIndex || itemMa.containsMouse)
                                   ? Theme.bgHover : "transparent"

                            RowLayout {
                                anchors { fill: parent; leftMargin: Theme.gapMd; rightMargin: Theme.gapMd }
                                spacing: Theme.gapSm

                                Label {
                                    text: chapterRow.modelData.title
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.sizeUi
                                    color: Theme.textPrimary
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: chapterRow.modelData.wordCount > 0 ? chapterRow.modelData.wordCount + " 字" : ""
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
                                onClicked: root.selectChapter(chapterRow.index)
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
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // 分段阅读列表：每段一个委托项，段距 = spacing（精确控制，不再是整行高的空行）。
            // 左/右留白在委托内用 x+width；上/下留白用官方 header/footer 机制
            // （doc.qt.io ListView：headerPositioning/footerPositioning 默认 InlineHeader/
            // InlineFooter，随内容滚动；positionViewAtBeginning 明确 "taking into account
            // any header or footer"）。
            // 注：topMargin/bottomMargin 是 Flickable 页定义的官方属性（ListView 继承），
            // 语义为"内容四周额外保留的边距"；此处采用 header/footer 是因其语义更贴合
            // "阅读区上下留白"且官方定位 API 明文计入，行为完全可预期。
            ListView {
                id: chapterList
                anchors.fill: parent
                clip: true
                model: root.paragraphs
                spacing: Theme.readerParagraphGap
                boundsBehavior: Flickable.StopAtBounds

                header: Item {
                    width: ListView.view.width
                    height: Theme.gapXl   // 顶部留白（随内容滚动，与 topMargin 视觉一致）
                }
                footer: Item {
                    width: ListView.view.width
                    height: Theme.gapXl   // 底部留白（随内容滚动）
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }

                // 官方委托模式：required property + ListView.view 附加属性
                // （doc.qt.io Models：数组模型经 modelData 提供数据，委托内取视图宽度
                // 用 ListView.view.width，而非引用外层 id——后者属未限定访问，Qt 6
                // 组件边界下不可靠，qmllint 亦告警）。
                // 注意：ListView 会接管委托根元素的 x/y（垂直列表 x 恒为 0），
                // 在根上写 x 无效（实测被覆盖、文字贴左边缘）；左右留白必须放在
                // 根内子项上，用 anchors + margins 实现。
                delegate: Item {
                    id: paraItem
                    required property string modelData
                    width: ListView.view.width
                    height: paraText.implicitHeight

                    Text {
                        id: paraText
                        anchors { left: parent.left; right: parent.right; margins: Theme.gapXl }
                        text: paraItem.modelData
                        font.family: Theme.fontDisplay
                        font.pixelSize: Theme.sizeBody + 1
                        lineHeight: Theme.lineHeightBody
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        color: Theme.textPrimary
                    }
                }
            }

            // 空状态垂直居中：旧版提示贴顶、下方大片空白，重心失衡
            Label {
                anchors.centerIn: parent
                visible: root.chapterContent.length === 0
                text: "从上方选择章节，或让墨染生成内容后在此阅读。"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeUi
                color: Theme.textFaint
            }
        }

        // ── 底部字数（无内容时隐藏，避免空占一条分割线）──
        Rectangle {
            Layout.fillWidth: true
            height: 26
            visible: root.chapterContent.length > 0
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
