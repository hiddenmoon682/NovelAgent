import QtQuick
import QtQuick.Layouts

// ChatBubble — 单条聊天消息气泡。
// 用户消息靠右（强调色），Agent 消息靠左（深色底 + 绿色标识）。
// 支持流式追加时的"正在输入"光标效果。
ColumnLayout {
    id: root

    property string role: "user"          // "user" | "assistant"
    property string content: ""
    property bool streaming: false        // 正在流式接收（显示光标）

    width: parent ? parent.width : 0
    spacing: Theme.gapXs

    readonly property bool isUser: role === "user"

    // 角色标签
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

    // 气泡主体
    Rectangle {
        Layout.alignment: root.isUser ? Qt.AlignRight : Qt.AlignLeft
        Layout.maximumWidth: root.width * 0.82
        Layout.leftMargin: root.isUser ? 0 : Theme.gapSm
        Layout.rightMargin: root.isUser ? Theme.gapSm : 0

        width: Math.min(bubbleText.implicitWidth + Theme.gapLg * 2,
                        root.width * 0.82)
        height: bubbleText.implicitHeight + Theme.gapMd * 2
        radius: Theme.radiusMd
        color: root.isUser ? Theme.accentSoft : Theme.bgElevated
        border.width: root.isUser ? 0 : 1
        border.color: Theme.divider

        // 左上/右上不对称圆角，区分方向
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
        }

        Text {
            id: bubbleText
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                margins: Theme.gapMd
            }
            text: root.content + (root.streaming ? "▍" : "")
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.sizeBody
            lineHeight: 1.6
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            color: root.isUser ? Theme.textPrimary : Theme.textPrimary

            // 流式光标闪烁
            SequentialAnimation on color {
                running: root.streaming
                loops: Animation.Infinite
                ColorAnimation { to: Theme.textSecondary; duration: 400 }
                ColorAnimation { to: Theme.textPrimary; duration: 400 }
            }
        }
    }
}
