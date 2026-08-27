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
        if (!it) return -1
        return (it.type === "message" && it.role === "assistant" && it.streaming) ? idx : -1
    }

    function appendAssistant(content, reasoning) {
        chatModel.append({ type: "message", role: "assistant", content: content,
                           reasoning: reasoning, streaming: true, toolName: "", toolStatus: "" })
    }

    function finalizeRunningTools(status) {
        for (var i = 0; i < chatModel.count; ++i) {
            var it = chatModel.get(i)
            if (it && it.type === "tool" && it.toolStatus === "running")
                chatModel.setProperty(i, "toolStatus", status)
        }
    }

    // 条目归属的发言方：工具卡片归属 assistant 回合。
    function turnOwner(it) {
        if (!it) return ""
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
        // 视口定位与加载解耦（文档依据 doc.qt.io Qt6 ListView/Flickable 协议）：
        // 启动早期 SplitView 首帧布局晚于 agentReadyChanged，chatView 宽高可能尚未
        // 定型（甚至为 0——0 宽高下 ListView 不加载任何 delegate，contentHeight=0，
        // 此时"滚动到底"实际等于"滚动到顶"）；且变量高度 delegate 下 contentHeight
        // 只是估计值，按临时值手写 contentY 会让视口停在不该停的位置（顶部空白/中间错位）。
        // 因此这里不设一次性定时器：改为事件驱动——布局/内容高度每次变化都触发
        // snapToEnd()，由它自身的"宽高已定型 + 内容高于视口"守卫决定是否锚定，
        // 布局稳定后必然落在底部；内容不足一屏则显式回顶，避免负偏移在首条上方空出空白。
        root.snapToEnd()
    }

    // 视口贴底锚定：仅在"用户未主动上翻"（chatView.userAtBottom）时执行。
    // 统一走官方 positionViewAtEnd()，不手写 contentY（官方点名手写定位会随
    // delegate 尺寸变化而失效）；内容不足一屏时显式回顶部，防止 positionViewAtEnd
    // 在短内容上产生负偏移，把首条消息上方空出一大片空白（用户可复现的"上部空白"）。
    // 宽高未定型（≤0）时跳过：此时 ListView 不加载 delegate，等 onWidth/HeightChanged
    // 或内容高度变化事件再触发，天然规避"用临时尺寸定位"的竞态。
    function snapToEnd() {
        if (!chatView.userAtBottom) return
        if (chatView.width <= 0 || chatView.height <= 0) return
        if (chatView.contentHeight > chatView.height)
            chatView.positionViewAtEnd()
        else
            chatView.contentY = 0
    }

    Component.onCompleted: reloadHistory()

    function sendCurrentMessage() {
        var text = inputField.text.trim()
        if (text.length === 0 || bridge.sessionBusy) return

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
                // 显式声明模型角色（required property）：替代隐式 model.* 作用域链，
                // 避免未来增删字段/重命名时漏改造成绑定静默失效（QML 未定义引用取默认值）
                required property string type
                required property string role
                required property string content
                required property string reasoning
                required property bool streaming
                required property string toolName
                required property string toolStatus
                // required property 会关闭隐式 index/modelData 上下文注入，须显式声明
                required property int index
                width: chatView.width - chatView.leftMargin - chatView.rightMargin
                // 发言方切换时才算新回合：加大段间距；同一回合内被工具卡片隔开的段落紧凑排列。
                // 模型在流式/切换会话时会被 clear/append/remove 频繁改动，index 可能短暂越界，
                // get() 返回 undefined，必须判空否则 turnOwner 报 "Value is undefined" 警告。
                readonly property bool newTurn: {
                    if (chatModel.count === 0) return false
                    if (index === 0) return true
                    if (index >= chatModel.count) return false
                    var prev = chatModel.get(index - 1)
                    var cur = chatModel.get(index)
                    return !!prev && !!cur && root.turnOwner(prev) !== root.turnOwner(cur)
                }
                height: loader.height + (newTurn && index > 0 ? Theme.gapMd : 0)

                Loader {
                    id: loader
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: item ? item.implicitHeight : 0
                    sourceComponent: type === "tool" ? toolComp : msgComp

                    Component {
                        id: msgComp
                        ChatBubble {
                            role: delegateRoot.role
                            content: delegateRoot.content
                            reasoning: delegateRoot.reasoning
                            streaming: delegateRoot.streaming
                        }
                    }
                    Component {
                        id: toolComp
                        ToolCallCard {
                            toolName: delegateRoot.toolName
                            status: delegateRoot.toolStatus
                        }
                    }
                }
            }

            add: Transition {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animNormal }
            }

            // 底部跟随开关（初始贴底）。官方语义：moving/flicking 仅在用户拖动/甩动
            // 期间为 true——程序化定位（positionViewAtEnd 等）、布局变化、动画、
            // 流式文本增长都不会置位它们。因此只有用户手势能让状态切到"上翻暂停"
            // （离开底部）或"翻回底部恢复跟随"，杜绝自动跟随被误关。
            property bool userAtBottom: true

            onContentYChanged: {
                if (moving || flicking)
                    userAtBottom = atYEnd
            }

            // 布局/内容变化后统一经根节点 snapToEnd() 重新锚定（事件驱动，
            // 无固定延时定时器的时序竞态：每次尺寸/内容变化都触发，最后一次
            // 稳定布局必然落在正确位置——贴底时到底部、内容不足一屏时到顶部）。
            onContentHeightChanged: {
                root.snapToEnd()
            }

            onHeightChanged: {
                root.snapToEnd()
            }

            onWidthChanged: {
                root.snapToEnd()
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
                    font.pixelSize: Theme.sizeHero
                    font.weight: Font.Bold
                    color: Theme.textPrimary
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "你的 AI 小说创作伙伴 — 构思、写作、管理设定"
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeUi
                    color: Theme.textSecondary
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
                                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                // hover 时箭头右移 4px 的微动画，提示可点击
                                anchors.rightMargin: cardMa.containsMouse ? Theme.gapMd + 4 : Theme.gapMd
                                text: "→"
                                font.pixelSize: Theme.sizeUi
                                color: cardMa.containsMouse ? Theme.accent : Theme.textFaint
                                Behavior on anchors.rightMargin { NumberAnimation { duration: Theme.animFast } }
                                Behavior on color { ColorAnimation { duration: Theme.animFast } }
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
                // IME 合成期间 preeditText 非空（而 text 仍为空），需一并视为"有输入"，
                // 否则中/日文输入法联拼时占位文案会一直盖在候选文字上，回车提交后才消失。
                visible: inputField.text.length === 0 && inputField.preeditText.length === 0
                // 生成中禁输入（sessionBusy 时发送按钮变"取消"）：占位文案同步提示，避免"输入了没反应"
                text: bridge.sessionBusy ? "正在生成中…"
                     : (bridge.agentReady ? "输入指令或问题..." : "请先完成模型配置（左下角设置）")
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

                // ── 技能入口：展示已启用数，点击打开管理弹窗 ──
                Rectangle {
                    id: skillBtn
                    visible: bridge.agentReady && bridge.projectPath.length > 0
                    width: skillBtnLabel.width + Theme.gapMd * 2
                    height: 26
                    radius: 13
                    color: skillMa.containsMouse || skillPopup.visible
                           ? Theme.bgHover : "transparent"
                    border.width: 1
                    border.color: skillPopup.visible ? Theme.accent : Theme.divider
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

                    property int enabledCount: 0
                    property int totalCount: 0

                    function refreshCount() {
                        var list = bridge.skillList()
                        totalCount = list.length
                        var n = 0
                        for (var i = 0; i < list.length; ++i)
                            if (list[i].enabled) n++
                        enabledCount = n
                    }

                    Component.onCompleted: refreshCount()
                    Connections {
                        target: bridge
                        function onSkillsChanged() { skillBtn.refreshCount() }
                        function onAgentReadyChanged() { skillBtn.refreshCount() }
                    }

                    Label {
                        id: skillBtnLabel
                        anchors.centerIn: parent
                        text: skillBtn.totalCount > 0
                              ? "✦ 技能 " + skillBtn.enabledCount + "/" + skillBtn.totalCount
                              : "✦ 技能"
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.sizeCaption
                        color: skillBtn.enabledCount > 0 ? Theme.textSecondary : Theme.textFaint
                    }
                    MouseArea {
                        id: skillMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: skillPopup.visible ? skillPopup.close() : skillPopup.open()
                    }
                }

                Label {
                    text: "Enter 发送 · Shift+Enter 换行"
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeCaption
                    color: Theme.textFaint
                }

                Item { Layout.fillWidth: true }

                ThemedButton {
                    id: sendBtn
                    kind: bridge.sessionBusy ? "danger" : "primary"
                    text: bridge.sessionBusy ? "取消" : "发送"
                    enabled: bridge.agentReady && (bridge.sessionBusy || inputField.text.trim().length > 0)
                    onClicked: {
                        if (bridge.sessionBusy) {
                            bridge.cancelRequest()
                        } else {
                            root.sendCurrentMessage()
                        }
                    }
                }
            }
        }
    }

    // 技能管理弹窗：锚在输入区上方
    SkillPopup {
        id: skillPopup
        parent: inputRect
        x: 0
        y: -height - Theme.gapSm

        onCreateSkillRequested: {
            inputField.text = "请使用 create-skill 技能，引导我创建一个新技能"
            root.sendCurrentMessage()
        }
    }

    Connections {
        target: bridge

        function onAgentReadyChanged() {
            root.reloadHistory()
        }

        function onSessionReset() {
            root.reloadHistory()  // 新建会话为空；切换/删除后加载目标会话历史
            // 切回"正在生成"的会话：进行中的回复尚未提交进 memory（完成时才落盘），
            // 重载后补一个空 streaming 占位，让后续 token 续写同一气泡，
            // 避免回复呈现"无头残片"（后台生成切回场景）。
            if (bridge.sessionBusy)
                root.appendAssistant("", "")
        }

        function onTokenReceived(sessionId, delta) {
            if (sessionId !== bridge.currentSessionId) return
            var idx = root.lastStreamingAssistant()
            if (idx >= 0)
                chatModel.setProperty(idx, "content", chatModel.get(idx).content + delta)
            else
                root.appendAssistant(delta, "")
        }

        function onReasoningReceived(sessionId, delta) {
            if (sessionId !== bridge.currentSessionId) return
            var idx = root.lastStreamingAssistant()
            if (idx >= 0)
                chatModel.setProperty(idx, "reasoning", chatModel.get(idx).reasoning + delta)
            else
                root.appendAssistant("", delta)
        }

        function onToolCallStarted(sessionId, toolName) {
            if (sessionId !== bridge.currentSessionId) return
            var idx = root.lastStreamingAssistant()
            if (idx >= 0) {
                var it = chatModel.get(idx)
                if (it && it.content.length === 0 && it.reasoning.length === 0)
                    chatModel.remove(idx)   // 空占位直接移除，避免残留空气泡
                else if (it)
                    chatModel.setProperty(idx, "streaming", false)
            }
            chatModel.append({ type: "tool", role: "", content: "", reasoning: "",
                               streaming: false, toolName: toolName, toolStatus: "running" })
        }

        function onToolCallFinished(sessionId, toolName, ok) {
            if (sessionId !== bridge.currentSessionId) return
            for (var i = chatModel.count - 1; i >= 0; --i) {
                var it = chatModel.get(i)
                if (it && it.type === "tool" && it.toolName === toolName && it.toolStatus === "running") {
                    chatModel.setProperty(i, "toolStatus", ok ? "ok" : "error")
                    return
                }
            }
        }

        function onResponseComplete(sessionId, fullText) {
            if (sessionId !== bridge.currentSessionId) return
            root.finalizeRunningTools("ok")
            var idx = root.lastStreamingAssistant()
            if (idx >= 0) {
                var it = chatModel.get(idx)
                if (it && it.content.length === 0 && it.reasoning.length === 0)
                    chatModel.remove(idx)
                else if (it)
                    chatModel.setProperty(idx, "streaming", false)
            }
        }

        function onErrorOccurred(sessionId, message) {
            // 会话维度过滤：空串 = 会话无关错误（显示在当前查看会话）；
            // 非空 = 仅正在查看该会话才展示（后台会话报错不污染当前视图）
            if (sessionId.length > 0 && sessionId !== bridge.currentSessionId) return
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
