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

    Component.onCompleted: reload()

    Connections {
        target: bridge
        function onChaptersChanged() { root.reload() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

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
}
