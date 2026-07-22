import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ReaderPanel — 右栏：小说文本展示（只读阅读视图）。
// Markdown 渲染，衬线字体，行高 1.9，左右留白书页效果。
ColumnLayout {
    id: root
    spacing: 0

    property string chapterTitle: "未选择章节"
    property string chapterContent: ""

    // ── 标题栏 ──
    Rectangle {
        Layout.fillWidth: true
        height: 48
        color: Theme.bgPanel

        RowLayout {
            anchors { fill: parent; leftMargin: Theme.gapLg; rightMargin: Theme.gapLg }
            Label {
                text: root.chapterTitle
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.sizeTitle
                font.weight: Font.DemiBold
                color: Theme.textPrimary
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                text: root.chapterContent.length > 0
                      ? root.chapterContent.length + " 字"
                      : ""
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textFaint
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
                  : "选择左侧章节或让 Agent 生成内容后，文本将在此处展示。\n\n你也可以直接在对话中要求墨染朗读或展示某个章节的内容。"
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
}
