import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    property string role: "user"
    property string content: ""
    property bool streaming: false

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

    Rectangle {
        id: bubbleRect
        Layout.alignment: root.isUser ? Qt.AlignRight : Qt.AlignLeft
        Layout.maximumWidth: root.width * 0.82
        Layout.leftMargin: root.isUser ? 0 : Theme.gapSm
        Layout.rightMargin: root.isUser ? Theme.gapSm : 0

        implicitWidth: bubbleText.contentWidth + Theme.gapMd * 2
        implicitHeight: bubbleText.contentHeight + Theme.gapXs * 2
        radius: Theme.radiusMd
        color: root.isUser ? Theme.accentSoft : Theme.bgElevated
        border.width: root.isUser ? 0 : 1
        border.color: Theme.divider

        Text {
            id: bubbleText
            x: Theme.gapMd
            y: Theme.gapXs
            width: root.width * 0.82 - Theme.gapMd * 2
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
    }
}
