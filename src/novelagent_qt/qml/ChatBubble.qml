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
    // 去掉尾部换行/空行：若保留，contentHeight 会把末行下方的空行也计入，气泡底部凭空多出空白，
    // 视觉上文本"偏上、下空隙大于上间隙"（与用户消息 displayText 的处理保持一致）。
    readonly property string formattedText: isUser ? displayText : mdWithHardBreaks(content).replace(/\n+$/, "")

    // CommonMark 把单换行当软换行合并为空格，导致模型输出的分行被揉成一段；
    // 这里在代码块之外把单换行转为硬换行（行尾双空格），保留原始分行结构。
    function mdWithHardBreaks(src) {
        var nl = String.fromCharCode(10)
        var parts = src.split("```")
        for (var i = 0; i < parts.length; i += 2) {   // 偶数段在代码块外
            var lines = parts[i].split(nl)
            for (var j = 0; j < lines.length - 1; ++j) {
                // 相邻两行都非空 → 之间是单换行，行尾补双空格转硬换行
                if (lines[j].length > 0 && lines[j + 1].length > 0)
                    lines[j] += "  "
            }
            parts[i] = lines.join(nl)
        }
        return parts.join("```")
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
                // 纯文本标题：💭 是仅 Emoji 呈现的码点，无法单色化，与暖墨单色主题冲突
                text: "思考过程"
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
        // 正文为空且已结束流式（如只有思考过程的段落）时不显示空气泡
        visible: root.content.length > 0 || root.streaming
        Layout.alignment: root.isUser ? Qt.AlignRight : Qt.AlignLeft
        Layout.maximumWidth: root.width * 0.82
        Layout.leftMargin: root.isUser ? 0 : Theme.gapSm
        Layout.rightMargin: root.isUser ? Theme.gapSm : 0

        // 气泡宽度按文本自然宽度收紧：此前用 Math.max(40, …) 做最小文本宽，
        // 短消息（如"你好"，文本仅 ~30px）也会被强撑到 40px 文本区→气泡约 64px，
        // 实际文本只占左侧、右侧多出一段死白。改用较小地板（留出 24px 保底），
        // 使气泡贴合文本、两侧留白对称。
        implicitWidth: Math.min(bubbleText.maxWidth, Math.max(Theme.gapMd * 2, textMeasurer.contentWidth)) + Theme.gapMd * 2
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
