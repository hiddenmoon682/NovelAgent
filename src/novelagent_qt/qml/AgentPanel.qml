import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    spacing: 0

    ListModel { id: chatModel }

    ListView {
        id: chatView
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        interactive: true
        spacing: Theme.gapMd
        topMargin: Theme.gapLg
        bottomMargin: Theme.gapLg
        leftMargin: Theme.gapLg
        rightMargin: Theme.gapLg

        model: chatModel
        delegate: ChatBubble {
            height: implicitHeight
            role: model.role
            content: model.content
            streaming: model.streaming === true
        }

        add: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animNormal }
        }

        property bool userAtBottom: true

        onContentYChanged: {
            if (moving || flicking)
                userAtBottom = atYEnd
        }

        onContentHeightChanged: {
            if (userAtBottom)
                contentY = Math.max(0, contentHeight - height)
        }

        onHeightChanged: {
            if (userAtBottom)
                contentY = Math.max(0, contentHeight - height)
        }

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
        id: inputRect
        Layout.fillWidth: true
        Layout.margins: Theme.gapMd
        implicitHeight: inputField.height + sendRow.height + Theme.gapMd * 2 + Theme.gapSm
        radius: Theme.radiusMd
        color: Theme.bgElevated
        border.color: inputField.activeFocus ? Theme.accent : Theme.divider
        border.width: 1

        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

        MouseArea {
            anchors.fill: parent
            onClicked: (mouse) => { inputField.forceActiveFocus() }
        }

        TextArea {
            id: inputField
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                topMargin: Theme.gapMd
                leftMargin: Theme.gapMd
                rightMargin: Theme.gapMd
            }
            height: Math.min(Math.max(implicitHeight, 24), 120)
            placeholderText: ""
            color: Theme.textPrimary
            font.family: Theme.fontUi
            font.pixelSize: Theme.sizeBody
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            leftPadding: 0
            rightPadding: 0
            topPadding: 4
            bottomPadding: 4
            background: Item {}

            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Return && !event.modifiers) {
                    event.accepted = true
                    sendCurrentMessage()
                }
            }
        }

        Label {
            anchors {
                left: inputField.left
                top: inputField.top
                topMargin: inputField.topPadding
            }
            visible: inputField.text.length === 0 && !inputField.activeFocus
            text: "输入指令或问题..."
            font.family: Theme.fontUi
            font.pixelSize: Theme.sizeBody
            color: Theme.textFaint
        }

        RowLayout {
            id: sendRow
            anchors {
                top: inputField.bottom
                left: parent.left
                right: parent.right
                topMargin: Theme.gapSm
                leftMargin: Theme.gapMd
                rightMargin: Theme.gapMd
                bottomMargin: Theme.gapSm
            }

            Item { Layout.fillWidth: true }

            Button {
                id: sendBtn
                text: bridge.busy ? "取消" : "发送"
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

    Connections {
        target: bridge

        function onTokenReceived(delta) {
            if (chatModel.count === 0) return
            var last = chatModel.get(chatModel.count - 1)
            if (last.role === "assistant") {
                if (!last.streaming && last.content.length > 0)
                    last.content += "\n\n"
                last.streaming = true
                last.content += delta
                chatModel.set(chatModel.count - 1, last)
            }
        }

        function onReasoningReceived(delta) {
        }

        function onResponseComplete(fullText) {
            if (chatModel.count === 0) return
            var last = chatModel.get(chatModel.count - 1)
            if (last.role === "assistant") {
                last.streaming = false
                chatModel.set(chatModel.count - 1, last)
            }
        }

        function onErrorOccurred(message) {
            chatModel.append({ role: "assistant", content: "⚠ " + message, streaming: false })
        }
    }

    function sendCurrentMessage() {
        var text = inputField.text.trim()
        if (text.length === 0 || bridge.busy) return

        chatModel.append({ role: "user", content: text, streaming: false })
        chatModel.append({ role: "assistant", content: "", streaming: true })

        inputField.text = ""
        bridge.sendMessage(text)
    }
}
