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

    // 同 ChatBubble：代码块外的单换行转硬换行，避免正文分段被 CommonMark 软换行规则合并。
    function mdWithHardBreaks(src) {
        var nl = String.fromCharCode(10)
        var parts = src.split("```")
        for (var i = 0; i < parts.length; i += 2) {
            var lines = parts[i].split(nl)
            for (var j = 0; j < lines.length - 1; ++j) {
                if (lines[j].length > 0 && lines[j + 1].length > 0)
                    lines[j] += "  "
            }
            parts[i] = lines.join(nl)
        }
        return parts.join("```")
    }

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
                      ? root.mdWithHardBreaks(root.chapterContent)
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
