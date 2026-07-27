import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// AgentPanel — 中栏：对话流（消息气泡 / 工具卡片）+ 空状态建议 + 输入区。
Rectangle {
    id: root
    color: Theme.bgChat

    // chatModel 条目统一字段：
    //   type: "message" | "tool"
    //   role/content/reasoning/streaming — message 条目使用
    //   toolName/toolStatus("running"|"ok"|"error") — tool 条目使用
    ListModel { id: chatModel }

    // 仅当「最后一条」是 streaming 中的 assistant 消息时返回其下标，否则 -1。
    // （工具卡片插入后，后续 token 应开启新气泡，而非回写旧气泡。）
    function lastStreamingAssistant() {
        var idx = chatModel.count - 1
        if (idx < 0) return -1
        var it = chatModel.get(idx)
        return (it.type === "message" && it.role === "assistant" && it.streaming) ? idx : -1
    }

    function appendAssistant(content, reasoning) {
        chatModel.append({ type: "message", role: "assistant", content: content,
                           reasoning: reasoning, streaming: true, toolName: "", toolStatus: "" })
    }

    function finalizeRunningTools(status) {
        for (var i = 0; i < chatModel.count; ++i) {
            var it = chatModel.get(i)
            if (it.type === "tool" && it.toolStatus === "running")
                chatModel.setProperty(i, "toolStatus", status)
        }
    }

    // 条目归属的发言方：工具卡片归属 assistant 回合。
    function turnOwner(it) {
        return it.type === "tool" ? "assistant" : it.role
    }

    // 从 bridge 重建聊天流（启动恢复上次对话 / 切换项目后刷新）。
    function reloadHistory() {
        chatModel.clear()
        if (!bridge.agentReady) return
        var hist = bridge.conversationHistory()
        for (var i = 0; i < hist.length; ++i) {
            chatModel.append({ type: "message", role: hist[i].role, content: hist[i].content,
                               reasoning: hist[i].reasoning, streaming: false,
                               toolName: "", toolStatus: "" })
        }
    }

    Component.onCompleted: reloadHistory()

    function sendCurrentMessage() {
        var text = inputField.text.trim()
        if (text.length === 0 || bridge.busy) return

        chatModel.append({ type: "message", role: "user", content: text,
                           reasoning: "", streaming: false, toolName: "", toolStatus: "" })
        appendAssistant("", "")

        inputField.text = ""
        bridge.sendMessage(text)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ListView {
            id: chatView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            interactive: true
            spacing: Theme.gapXs
            topMargin: Theme.gapLg
            bottomMargin: Theme.gapLg
            leftMargin: Theme.gapLg
            rightMargin: Theme.gapLg

            model: chatModel
            delegate: Item {
                id: delegateRoot
                width: chatView.width - chatView.leftMargin - chatView.rightMargin
                // 发言方切换时才算新回合：加大段间距并显示名字标签；
                // 同一回合内被工具卡片隔开的段落紧凑排列，读起来是一条完整回复。
                readonly property bool newTurn: chatModel.count > 0 &&
                    (index === 0 || root.turnOwner(chatModel.get(index - 1)) !== root.turnOwner(chatModel.get(index)))
                height: loader.height + (newTurn && index > 0 ? Theme.gapMd : 0)

                Loader {
                    id: loader
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: item ? item.implicitHeight : 0
                    sourceComponent: model.type === "tool" ? toolComp : msgComp

                    Component {
                        id: msgComp
                        ChatBubble {
                            role: model.role
                            content: model.content
                            reasoning: model.reasoning
                            streaming: model.streaming === true
                            showHeader: delegateRoot.newTurn
                        }
                    }
                    Component {
                        id: toolComp
                        ToolCallCard {
                            toolName: model.toolName
                            status: model.toolStatus
                        }
                    }
                }
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

            // ── 空状态 ──
            Column {
                anchors.centerIn: parent
                visible: chatModel.count === 0
                spacing: Theme.gapLg

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "墨染"
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.sizeDisplay + 12
                    font.weight: Font.Bold
                    color: Theme.textPrimary
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "你的 AI 小说创作伙伴 — 构思、写作、管理设定"
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeUi
                    color: Theme.textFaint
                }

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.gapSm

                    Repeater {
                        model: [
                            { title: "开始一部新小说", prompt: "我想开始一部新小说，请帮我构思大纲、角色和世界观" },
                            { title: "创作新章节",     prompt: "根据现有大纲和设定，继续写下一章" },
                            { title: "构建世界观",     prompt: "帮我完善这部小说的世界观设定" }
                        ]
                        delegate: Rectangle {
                            width: 320
                            height: 44
                            radius: Theme.radiusMd
                            color: cardMa.containsMouse ? Theme.bgHover : Theme.bgElevated
                            border.width: 1
                            border.color: Theme.divider
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }

                            Label {
                                anchors { left: parent.left; leftMargin: Theme.gapMd; verticalCenter: parent.verticalCenter }
                                text: modelData.title
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.sizeUi
                                color: Theme.textPrimary
                            }
                            Label {
                                anchors { right: parent.right; rightMargin: Theme.gapMd; verticalCenter: parent.verticalCenter }
                                text: "\u2192"
                                font.pixelSize: Theme.sizeUi
                                color: Theme.textFaint
                            }
                            MouseArea {
                                id: cardMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    inputField.text = modelData.prompt
                                    inputField.forceActiveFocus()
                                }
                            }
                        }
                    }
                }
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
                        root.sendCurrentMessage()
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
                text: bridge.agentReady ? "输入指令或问题..." : "请先完成模型配置（左下角设置）"
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

                Label {
                    text: "Enter 发送 · Shift+Enter 换行"
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeCaption
                    color: Theme.textFaint
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: sendBtn
                    text: bridge.busy ? "取消" : "发送"
                    enabled: bridge.agentReady && (bridge.busy || inputField.text.trim().length > 0)
                    onClicked: {
                        if (bridge.busy) {
                            bridge.cancelRequest()
                        } else {
                            root.sendCurrentMessage()
                        }
                    }

                    contentItem: Text {
                        text: sendBtn.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.sizeUi
                        color: sendBtn.enabled ? "#f5efe2" : Theme.textFaint
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
    }

    Connections {
        target: bridge

        function onAgentReadyChanged() {
            root.reloadHistory()
        }

        function onSessionReset() {
            root.reloadHistory()  // 新建会话为空；切换/删除后加载目标会话历史
        }

        function onTokenReceived(delta) {
            var idx = root.lastStreamingAssistant()
            if (idx >= 0)
                chatModel.setProperty(idx, "content", chatModel.get(idx).content + delta)
            else
                root.appendAssistant(delta, "")
        }

        function onReasoningReceived(delta) {
            var idx = root.lastStreamingAssistant()
            if (idx >= 0)
                chatModel.setProperty(idx, "reasoning", chatModel.get(idx).reasoning + delta)
            else
                root.appendAssistant("", delta)
        }

        function onToolCallStarted(toolName) {
            var idx = root.lastStreamingAssistant()
            if (idx >= 0) {
                var it = chatModel.get(idx)
                if (it.content.length === 0 && it.reasoning.length === 0)
                    chatModel.remove(idx)   // 空占位直接移除，避免残留空气泡
                else
                    chatModel.setProperty(idx, "streaming", false)
            }
            chatModel.append({ type: "tool", role: "", content: "", reasoning: "",
                               streaming: false, toolName: toolName, toolStatus: "running" })
        }

        function onToolCallFinished(toolName, ok) {
            for (var i = chatModel.count - 1; i >= 0; --i) {
                var it = chatModel.get(i)
                if (it.type === "tool" && it.toolName === toolName && it.toolStatus === "running") {
                    chatModel.setProperty(i, "toolStatus", ok ? "ok" : "error")
                    return
                }
            }
        }

        function onResponseComplete(fullText) {
            root.finalizeRunningTools("ok")
            var idx = root.lastStreamingAssistant()
            if (idx >= 0) {
                var it = chatModel.get(idx)
                if (it.content.length === 0 && it.reasoning.length === 0)
                    chatModel.remove(idx)
                else
                    chatModel.setProperty(idx, "streaming", false)
            }
        }

        function onErrorOccurred(message) {
            root.finalizeRunningTools("error")
            var idx = root.lastStreamingAssistant()
            if (idx >= 0)
                chatModel.setProperty(idx, "streaming", false)
            chatModel.append({ type: "message", role: "assistant",
                               content: "⚠ " + message, reasoning: "",
                               streaming: false, toolName: "", toolStatus: "" })
        }
    }
}
