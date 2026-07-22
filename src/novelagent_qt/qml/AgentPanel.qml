import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// AgentPanel — 中栏对话面板。
// 消息气泡列表 + 输入框，流式逐 token 追加，自动滚底。
ColumnLayout {
    id: root
    spacing: 0

    // ── 消息模型 ──
    ListModel { id: chatModel }

    // ── 消息列表 ──
    ListView {
        id: chatView
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: Theme.gapMd
        topMargin: Theme.gapLg
        bottomMargin: Theme.gapLg
        leftMargin: Theme.gapLg
        rightMargin: Theme.gapLg

        model: chatModel
        delegate: ChatBubble {
            width: chatView.width - Theme.gapLg * 2
            role: model.role
            content: model.content
            streaming: model.streaming === true
        }

        // 新消息淡入
        add: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animNormal }
            NumberAnimation { property: "y"; from: chatView.height; duration: Theme.animNormal; easing.type: Easing.OutCubic }
        }

        // 自动滚底
        onContentHeightChanged: {
            if (contentHeight > height)
                contentY = contentHeight - height
        }

        // 空状态提示
        Label {
            anchors.centerIn: parent
            visible: chatModel.count === 0
            text: "开始对话，让墨染帮你构思、写作、管理设定"
            font.family: Theme.fontUi
            font.pixelSize: Theme.sizeUi
            color: Theme.textFaint
        }
    }

    // ── 输入区 ──
    Rectangle {
        Layout.fillWidth: true
        Layout.margins: Theme.gapMd
        height: inputRow.implicitHeight + Theme.gapMd * 2
        radius: Theme.radiusMd
        color: Theme.bgElevated
        border.color: inputField.activeFocus ? Theme.accent : Theme.divider
        border.width: 1

        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

        RowLayout {
            id: inputRow
            anchors {
                fill: parent
                margins: Theme.gapSm
            }
            spacing: Theme.gapSm

            TextArea {
                id: inputField
                Layout.fillWidth: true
                Layout.maximumHeight: 120
                placeholderText: "输入指令或问题..."
                placeholderTextColor: Theme.textFaint
                color: Theme.textPrimary
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeUi
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                background: Item {}
                padding: Theme.gapXs

                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Return && !event.modifiers) {
                        event.accepted = true
                        sendCurrentMessage()
                    }
                }
            }

            Button {
                id: sendBtn
                text: bridge.busy ? "取消" : "发送"
                Layout.alignment: Qt.AlignBottom
                enabled: bridge.busy || inputField.text.trim().length > 0
                onClicked: {
                    if (bridge.busy) {
                        bridge.cancelRequest()
                    } else {
                        sendCurrentMessage()
                    }
                }

                contentItem: Text {
                    text: sendBtn.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeUi
                    color: sendBtn.enabled ? "#ffffff" : Theme.textFaint
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    implicitWidth: 64
                    implicitHeight: 32
                    radius: Theme.radiusSm
                    color: bridge.busy ? Theme.danger
                         : sendBtn.enabled ? Theme.accent
                         : Theme.bgHover
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }
            }
        }
    }

    // ── 流式信号接线 ──
    Connections {
        target: bridge

        function onTokenReceived(delta) {
            if (chatModel.count === 0) return
            var last = chatModel.get(chatModel.count - 1)
            if (last.role === "assistant") {
                last.content += delta
                chatModel.set(chatModel.count - 1, last)
            }
        }

        function onReasoningReceived(delta) {
            // 推理过程暂不显示在主气泡中（可后续扩展为折叠区）
        }

        function onResponseComplete(fullText) {
            if (chatModel.count === 0) return
            var last = chatModel.get(chatModel.count - 1)
            if (last.role === "assistant") {
                last.streaming = false
                if (fullText.length > 0)
                    last.content = fullText
                chatModel.set(chatModel.count - 1, last)
            }
        }

        function onErrorOccurred(message) {
            chatModel.append({ role: "assistant", content: "⚠ " + message, streaming: false })
        }
    }

    // ── 发送逻辑 ──
    function sendCurrentMessage() {
        var text = inputField.text.trim()
        if (text.length === 0 || bridge.busy) return

        // 追加用户消息
        chatModel.append({ role: "user", content: text, streaming: false })
        // 追加空的 assistant 占位（流式填充）
        chatModel.append({ role: "assistant", content: "", streaming: true })

        inputField.text = ""
        bridge.sendMessage(text)
    }
}
