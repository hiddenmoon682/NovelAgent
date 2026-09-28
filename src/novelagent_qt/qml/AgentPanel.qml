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
    //   reasoningExpanded — message 条目使用（思考过程展开态，存模型以免滚动/重建丢失）
    //   toolName/toolStatus("running"|"ok"|"error") — tool 条目使用
    //   toolExpanded — tool 条目使用（同上，展开态存模型）
    //
    // 会话切换（reloadHistory）时**整体换成一个全新的 ListModel**，而不是对同一个
    // ListModel 先 clear() 再逐条 append()：同一轮事件循环内的"清空 + 重填"会让
    // QQuickListView 留下陈旧 delegate（旧坐标/空内容、索引映射失效），表现为切换
    // 会话后对话区整片空白（已用真机复现：contentItem 里只剩 h=30 的孤儿条目、无
    // 新 delegate 生成）。替换 model 是官方的"整体重建"语义，视图会完整重置。
    Component { id: chatModelComp; ListModel {} }
    property ListModel chatModel: chatModelComp.createObject(root) as ListModel

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
                           reasoning: reasoning, streaming: true, toolName: "", toolStatus: "",
                           toolArgs: "", toolResult: "", toolExpanded: false,
                           reasoningExpanded: false })
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

    // 剪贴板共享实例的执行端：气泡通过 copyRequested 上报文本（见 ChatBubble 该信号处的说明）。
    // delegate 里最重的额外 Item 就是各带一个隐藏 TextEdit，改为面板级单实例。
    function copyToClipboard(text) {
        clipboardHelper.text = text
        clipboardHelper.selectAll()
        clipboardHelper.copy()
    }

    // 从 bridge 重建聊天流（启动恢复上次对话 / 切换项目后刷新）。
    // 走"换新 model"而不是 clear()+append()：见 chatModel 属性处的说明。
    function reloadHistory() {
        var next = chatModelComp.createObject(root)
        if (!next) return
        if (bridge.agentReady) {
            var hist = bridge.conversationHistory()
            for (var i = 0; i < hist.length; ++i) {
                if (hist[i].type === "tool") {
                    // 历史回放重建工具调用条目（重启/切会话后仍显示工具卡片）：
                    // status 为终态（ok/error），参数/结果来自持久化的 tool_calls 与结果消息
                    next.append({ type: "tool", role: "", content: "", reasoning: "",
                                  streaming: false, toolName: hist[i].toolName,
                                  toolStatus: hist[i].toolStatus, toolArgs: hist[i].toolArgs,
                                  toolResult: hist[i].toolResult, toolExpanded: false,
                                  reasoningExpanded: false })
                } else {
                    next.append({ type: "message", role: hist[i].role, content: hist[i].content,
                                  reasoning: hist[i].reasoning, streaming: false,
                                  toolName: "", toolStatus: "", toolArgs: "", toolResult: "",
                                  toolExpanded: false, reasoningExpanded: false })
                }
            }
        }
        var previous = root.chatModel
        // 切会话/换项目 = 换看另一段对话：视口跟随开关必须一并复位为「贴底」。
        // userAtBottom 只在用户手势（moving/flicking）时更新，一旦在上一会话里上翻过就
        // 永久粘滞为 false；而整表换 model 会让 ListView 把视口重置到内容开头（qml.exe
        // 最小复现实测：contentY 回到 -topMargin、可见首条=0..4、末条甚至未创建），
        // 两者叠加的后果正是"切换会话后停在最旧的消息上"，且此后内容高度变化也不会再
        // 锚定（snapToEnd 被该标志挡住），视口会一直停在开头。
        chatView.userAtBottom = true
        // 【整表换 model 时必须临时关掉 reuseItems】Qt 6.8.3 实测（tests/probe_list_swap_fix.qml，
        // 单变量对照）：ListView 开启 reuseItems 时，**整表换 model（或 clear+append 全量重填）之后，
        // 已实例化的委托不会被重新绑定到新 model**——contentHeight 会更新成新内容的尺寸（实测 1438→142），
        // 但屏幕上仍是上一个 model 的行与旧数据（实测 model 只有 4 条时，视口内仍有 12 行可见、
        // 且标签全是旧 model 的）。表现即"点了侧栏切换会话后，中栏还显示上一个会话的内容"，
        // 并因新内容更短而下方留白（用户截图复现）。
        // 已排除的修法：先 `model = null` 再赋值无效；延后销毁旧 model 无效；换 model 后 forceLayout 无效。
        // 只有 reuseItems 这一个变量决定对错 → 换 model 前关闭、换完下一轮恢复：
        // 既修掉串档，又保住滚动时的委托复用（那才是当初开 reuseItems 的目的）。
        // 注意：callLater 在此安全——本组件与 AgentPanel 同生命周期，不像 delegate 会被销毁重建。
        chatView.reuseItems = false
        root.chatModel = next     // 视图整表切换（旧 delegate 全量销毁重建）
        if (previous) previous.destroy()
        Qt.callLater(function () { chatView.reuseItems = true })
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
    // 定位一律用官方 API（positionViewAtEnd / positionViewAtBeginning）——官方点名不要用
    // contentX/contentY 手工定位（落点会随 delegate 尺寸变化而失效），本函数已无任何手工 contentY。
    // 宽高未定型（≤0）时跳过：此时 ListView 不加载 delegate，等 onWidth/HeightChanged
    // 或内容高度变化事件再触发，天然规避"用临时尺寸定位"的竞态。
    function snapToEnd() {
        if (!chatView.userAtBottom) return
        if (chatView.width <= 0 || chatView.height <= 0) return
        // 用户手势进行中绝不重新锚定。Qt 侧两处硬事实叠加成"贴底后第一次滚轮上翻只挪一点点"：
        // ① 滚轮滚动在 Qt6 是**一段动画中的移动**（Flickable 滚轮分支 timeline.moveBy(…，
        //    3*fixupDuration/4 ≈ 300ms)），而程序化写 contentY 会先 movementEnding(false,true)
        //    再 resetTimeline（qquickflickable.cpp setContentY）——即**掐断用户这次滚动**；
        // ② ListView 的重填（refillOrLayout → 创建首屏外 delegate → 重算 averageSize →
        //    contentHeight 变化 → contentHeightChanged）发生在 contentItem 几何变化触发的
        //    viewportMoved 里，**早于** contentYChanged 的发出（qquickflickable.cpp
        //    itemGeometryChanged：先 viewportMoved/updateBeginningEnd，后 emit contentYChanged）。
        // 于是第一帧的顺序是：内容上移 → 重填 → contentHeightChanged → 本函数把视口锚回底部
        // 并掐断动画 → 才轮到 onContentYChanged 去更新 userAtBottom。中途掐断的结果就是"只挪
        // 了几像素"。Flickable 自身的内容尺寸变化修正同样有 `!pressed && !moving` 守卫
        // （setContentHeight），这里补上等价守卫；会话切换等程序化定位时 moving/flicking 为假，
        // 锚定照常生效。
        if (chatView.moving || chatView.flicking) return
        // 定位只用官方 API：贴底 positionViewAtEnd()，不足一屏时 positionViewAtBeginning()。
        // 上下留白已由 header/footer 占位做进内容（见 ListView 上 margin 处的说明），于是：
        //   · 原先贴底要手写 `contentY += bottomMargin`——因为 margin 不参与 positionViewAtEnd()
        //     的落点、却参与 ListView 自身的滚轮钳制（实测差 16px），不改就会出现"已经贴底、
        //     继续下滚还能再挪 16px"；
        //   · 原先回顶要手写 `contentY = -topMargin`——因为内容不足一屏时 positionViewAtEnd()
        //     会产生负偏移（首条上方空一大片）。
        // 现在留白就是内容（header/footer 各 16px，参与 contentHeight 与定位计算），
        // 官方 API 的落点与钳制末端天然一致，两处手工 contentY 全部删除。
        if (chatView.contentHeight > chatView.height)
            chatView.positionViewAtEnd()
        else
            chatView.positionViewAtBeginning()
    }

    // ── 流式写入缓冲（token 节流）──
    // 【为什么必须缓冲】Qt 官方性能文档："Calculating text layouts can be a slow operation…
    // consider using the PlainText format instead of StyledText wherever possible"；而气泡正文是
    // Text.MarkdownText，**每次 text 变化都要重新解析整段 markdown 并重排全文**，Qt 没有分块缓存。
    // 真机实测（tests/probe_chat_scroll.qml，真机 1707x1019 窗口、240Hz 屏、一帧预算 4.17ms）：
    //   可见气泡正文字数 → 单次写入代价
    //     500 字 → 3.1ms      2000 字 → 5.2ms
    //    8000 字 → 10.9ms    20000 字 → 18.9ms（峰值 29.4ms）
    // 即：正文超过约 500 字后，**每个 token 都要吃掉 1～7 帧**，表现为滚动/阅读时"时不时突然卡一下"。
    // 主流客户端同源做法：Vercel AI SDK 官方 recipe 把"每个 token 重新渲染 markdown"列为必须
    // memoize 的反模式；VS Code 把流式 markdown 渲染改成 rAF 批量。
    // 【做法】delta 先进纯 JS 字符串（不进模型、不触发任何绑定），由 flushTimer 按固定节奏
    // （16ms ≈ 60fps 一帧）合并成一次 setProperty —— 一秒 25 个 token 就从 25 次全量重排压到
    // 最多 60 次/秒的**合并**写入，且写入频率与渲染帧率解耦（不会超过屏幕刷新率）。
    // 【语义不变】content/reasoning 仍是严格单调追加，最终文本与逐 token 写入完全一致。
    property string tokenBuf: ""
    property string reasoningBuf: ""
    property bool bufStale: false        // 缓冲已失效（切会话/复位）：下次 flush 直接丢弃
    property string bufSessionId: ""     // 缓冲归属会话（仅用于入队时判断是否换了会话）

    function bufferToken(sessionId, delta, isReasoning) {
        // 会话已变（切换/新建/删除）：上一会话的残留缓冲直接丢弃，不污染新会话。
        // 判断放在这里（入队时已知 sessionId），flush 侧就不必再读 bridge 上下文属性。
        if (sessionId !== root.bufSessionId) {
            root.bufSessionId = sessionId
            root.tokenBuf = ""
            root.reasoningBuf = ""
            root.bufStale = false
        }
        if (root.bufStale) return       // 复位后旧会话仍在推 token：忽略
        if (isReasoning) root.reasoningBuf += delta
        else root.tokenBuf += delta
        if (!flushTimer.running) flushTimer.start()
    }

    function flushTokens() {
        if (root.tokenBuf.length === 0 && root.reasoningBuf.length === 0) return
        if (root.bufStale) {            // 已切走/复位：丢弃，避免串写进新会话
            root.tokenBuf = ""
            root.reasoningBuf = ""
            return
        }
        var idx = root.lastStreamingAssistant()
        var c = root.tokenBuf, r = root.reasoningBuf
        root.tokenBuf = ""
        root.reasoningBuf = ""
        if (idx >= 0) {
            // 一次 setProperty 写入合并后的全部增量（正文与思考过程互不覆盖）
            var it = root.chatModel.get(idx)
            if (c.length > 0) root.chatModel.setProperty(idx, "content", it.content + c)
            if (r.length > 0) root.chatModel.setProperty(idx, "reasoning", it.reasoning + r)
        } else {
            root.appendAssistant(c, r)
        }
    }

    Timer {
        id: flushTimer
        interval: 16          // ≈ 一帧（60fps）；与渲染节奏对齐，不再一次 token 一次全量重排
        repeat: true
        onTriggered: root.flushTokens()
    }

    Component.onCompleted: reloadHistory()


    function sendCurrentMessage() {
        var text = inputField.text.trim()
        if (text.length === 0 || bridge.sessionBusy) return

        chatModel.append({ type: "message", role: "user", content: text,
                           reasoning: "", streaming: false, toolName: "", toolStatus: "",
                           toolArgs: "", toolResult: "", toolExpanded: false,
                           reasoningExpanded: false })
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
            // 边界硬停。官方四档语义见 doc.qt.io Flickable#boundsBehavior：默认
            // DragAndOvershootBounds 允许内容拖出边界、甩动越界后回弹。文档只提到
            // "拖动/甩动"越界，但**实测滚轮滚动在末端同样越界**——官方只读属性
            // verticalOvershoot 峰值 56px，每个滚轮刻度都会触发一次"越界→回弹"，
            // 表现为"滚到底部后对话区剧烈上下抖动"。StopAtBounds 后越界恒为 0。
            // 与本项目其余三个列表（ReaderPanel / ChapterDrawer / SkillPopup）取值一致。
            boundsBehavior: Flickable.StopAtBounds
            // ── 变高 + 高代价 delegate 的缓存策略（滚动"一卡一卡"与"首次上翻只挪一点点"同源）──
            // 会话气泡单条可达数百 px（长正文），而 cacheBuffer 默认只有 320px（≈半屏）：
            // ① 每滚过一屏就新建/销毁一批 delegate，而新建一条的代价很高——三遍正则归一化
            //    （normalizeKeycapEmoji / mdWithHardBreaks / fixCjkStrong，长正文数千字符）
            //    + 一次 markdown 排版 + ColumnLayout/Loader/隐藏 TextEdit 构造 → 掉帧；
            // ② 视口外条目按 averageSize 估算高度（官方 ListView "Variable Delegate Size"：
            //    "the rest are assumed to be of similar size"），滚动时才被真实高度替换，
            //    内容高度与 ListView 的 originY 随之漂移，Qt 会据此修正/钳回视口 ——
            //    这正是"贴底后第一次上翻只挪一点点"（修正把这次滚动吃掉了）。
            // 官方给的缓解手段就是调大 cacheBuffer。这里缓存前后各约一屏：够长的会话在滚到
            // 该区域时高度已是真实值，估算漂移基本消失；短会话（不足内容高度 + 两屏）干脆
            // 全部实例化，不存在估算。
            cacheBuffer: Math.max(600, chatView.height)
            // delegate 构造代价高（上面的三遍正则 + markdown 排版），开启回收复用后滚动
            // 不再反复重建；本 delegate 全部走 required property + 绑定，无 Component.onCompleted
            // 之类的构造期状态，满足官方的复用前提。
            reuseItems: true
            spacing: Theme.gapXs
            // ── 上下留白做进内容（header/footer 占位），不再用 Flickable 的 top/bottomMargin ──
            // 官方 ListView 文档明确："It is not recommended to use contentX or contentY to position
            // the view at a particular index… the actual start of the view can vary based on the size
            // of the delegates."；而 margin 按官方定义是"内容之外**额外**保留的空间"
            // （reserved in addition to contentWidth/Height）——它不参与 positionViewAtEnd() 的落点，
            // 却参与 ListView 自身的滚动钳制（真机实测差 16px），这正是原先必须手写
            // `contentY += bottomMargin`（贴底）与 `contentY = -topMargin`（回顶）的原因。
            // 改成 header/footer 占位后，留白本身成为内容的一部分：官方定位 API 的落点与钳制末端
            // 天然一致，两处手工 contentY 全部删除（见 snapToEnd）。
            topMargin: 0
            bottomMargin: 0
            header: Item { height: Theme.gapLg }
            footer: Item { height: Theme.gapLg }
            // 左右仍用 margin：水平方向不参与竖直钳制/定位，无需搬进内容
            leftMargin: Theme.gapLg
            rightMargin: Theme.gapLg

            model: root.chatModel
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
                required property string toolArgs
                required property string toolResult
                required property bool toolExpanded
                required property bool reasoningExpanded
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
                            // 思考过程展开态同样存 model 条目（理由与工具卡片一致，见下）：
                            // 否则滚出视口 delegate 被销毁后，展开的思考过程会自己折叠回去。
                            reasoningExpanded: delegateRoot.reasoningExpanded
                            onExpandedToggled: {
                                if (delegateRoot.index >= 0 && delegateRoot.index < chatModel.count) {
                                    var it = chatModel.get(delegateRoot.index)
                                    if (it && it.type === "message")
                                        chatModel.setProperty(delegateRoot.index, "reasoningExpanded",
                                                              !delegateRoot.reasoningExpanded)
                                }
                            }
                            // 复制走面板级共享 TextEdit（见 copyToClipboard），delegate 里不再有
                            // 隐藏 TextEdit —— 官方性能文档：delegate 只留立刻需要的元素
                            onCopyRequested: function(text) { root.copyToClipboard(text) }
                        }
                    }
                    Component {
                        id: toolComp
                        ToolCallCard {
                            toolName: delegateRoot.toolName
                            status: delegateRoot.toolStatus
                            toolArgs: delegateRoot.toolArgs
                            toolResult: delegateRoot.toolResult
                            // 展开态存 model 条目：ListView 滚动回收 delegate 时组件
                            // 实例会被复用，局部状态会残留到别的条目上；写入模型可
                            // 保证每个条目的展开态独立且随历史重建归零。
                            expanded: delegateRoot.toolExpanded
                            onExpandedToggled: {
                                // 防模型清空/切换会话期间 index 越界（与 newTurn 同守卫）
                                if (delegateRoot.index >= 0 && delegateRoot.index < chatModel.count) {
                                    var it = chatModel.get(delegateRoot.index)
                                    if (it && it.type === "tool")
                                        chatModel.setProperty(delegateRoot.index, "toolExpanded",
                                                              !delegateRoot.toolExpanded)
                                }
                            }
                        }
                    }
                }
            }

            add: Transition {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animNormal }
            }

            // 底部跟随开关（初始贴底）。官方语义：moving/flicking 表示"内容正因用户操作
            // 而移动"——程序化定位（positionViewAtEnd 等）、布局变化、动画、流式文本增长
            // 都不会置位它们。因此只有用户手势能让状态切到"上翻暂停"（离开底部）或
            // "翻回底部恢复跟随"，杜绝自动跟随被程序化定位误关。
            // 实测补充（qml.exe 探针采样统计）：滚轮滚动**同样会置位 moving**
            // （居中滚动 221/227 个采样点为真），所以滚轮用户上翻也会正确切到"上翻暂停"；
            // 末端越界回弹阶段 moving 为假且 atYEnd 为真，故不会误翻成"暂停跟随"。
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
                visible: root.chatModel.count === 0
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
            // 会话复位：丢弃上一会话残留的流式缓冲（否则旧 token 会被 flush 进新会话的气泡）。
            // 置 bufStale 而非只清空：复位后旧会话若仍有在途 token 到达，bufferToken 会据
            // bufSessionId 不符重新开始累积，故这里只需保证"已入队的那部分"不落进新会话。
            root.tokenBuf = ""
            root.reasoningBuf = ""
            root.bufStale = true
            root.bufSessionId = ""
            root.reloadHistory()  // 新建会话为空；切换/删除后加载目标会话历史
            // 切回"正在生成"的会话：进行中的回复尚未提交进 memory（完成时才落盘），
            // 重载后补一个空 streaming 占位，让后续 token 续写同一气泡，
            // 避免回复呈现"无头残片"（后台生成切回场景）。
            if (bridge.sessionBusy)
                root.appendAssistant("", "")
        }

        function onTokenReceived(sessionId, delta) {
            if (sessionId !== bridge.currentSessionId) return
            root.bufferToken(sessionId, delta, false)
        }

        function onReasoningReceived(sessionId, delta) {
            if (sessionId !== bridge.currentSessionId) return
            root.bufferToken(sessionId, delta, true)
        }

        function onToolCallStarted(sessionId, toolName, toolArgs) {
            if (sessionId !== bridge.currentSessionId) return
            // 先落地缓冲：本回合可能还有未 flush 的 token，若直接判空会把有内容的
            // 占位气泡误判为"空回复"并删除（丢字），故必须先 flush 再收尾。
            root.flushTokens()
            var idx = root.lastStreamingAssistant()
            if (idx >= 0) {
                var it = chatModel.get(idx)
                if (it && it.content.length === 0 && it.reasoning.length === 0)
                    chatModel.remove(idx)   // 空占位直接移除，避免残留空气泡
                else if (it)
                    chatModel.setProperty(idx, "streaming", false)
            }
            chatModel.append({ type: "tool", role: "", content: "", reasoning: "",
                               streaming: false, toolName: toolName, toolStatus: "running",
                               toolArgs: toolArgs, toolResult: "", toolExpanded: false,
                               reasoningExpanded: false })
        }

        function onToolCallFinished(sessionId, toolName, ok, result) {
            if (sessionId !== bridge.currentSessionId) return
            for (var i = chatModel.count - 1; i >= 0; --i) {
                var it = chatModel.get(i)
                if (it && it.type === "tool" && it.toolName === toolName && it.toolStatus === "running") {
                    chatModel.setProperty(i, "toolStatus", ok ? "ok" : "error")
                    chatModel.setProperty(i, "toolResult", result)
                    return
                }
            }
        }

        function onResponseComplete(sessionId, fullText) {
            if (sessionId !== bridge.currentSessionId) return
            // 先落地缓冲再收尾：否则末尾若干 token 会被后续 flush 追加到已定稿的气泡上
            // （streaming 已置 false，看起来像"回复结束后又蹦出几个字"）。
            root.flushTokens()
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
            root.flushTokens()   // 同上：错误收尾前先落地缓冲，避免"有内容却被判空删除"
            root.finalizeRunningTools("error")
            var idx = root.lastStreamingAssistant()
            if (idx >= 0) {
                // 与 onResponseComplete / onToolCallStarted 同口径：还没产出任何内容的
                // "空回复占位"直接移除，只留下面那条 ⚠ 提示。此前只把 streaming 置 false，
                // 空占位会留在列表里——气泡自身不渲染（visible 依赖 content/streaming），
                // 但该条目仍在，`newTurn` 为真时白占一个 gapMd(12px) 的空隙，
                // 且与另两条收尾路径行为不一致。
                var it = chatModel.get(idx)
                if (it && it.content.length === 0 && it.reasoning.length === 0)
                    chatModel.remove(idx)
                else
                    chatModel.setProperty(idx, "streaming", false)
            }
            chatModel.append({ type: "message", role: "assistant",
                               content: "⚠ " + message, reasoning: "",
                               streaming: false, toolName: "", toolStatus: "",
                               toolArgs: "", toolResult: "", toolExpanded: false,
                               reasoningExpanded: false })
        }
    }

    // ── 剪贴板共享实例（全面板一个）──
    // 原先每条气泡各带一个隐藏 TextEdit（delegate 里最重的额外 Item：每实例一份
    // QTextDocument + 光标）。气泡改为上报 copyRequested，复制动作集中在这里执行。
    TextEdit {
        id: clipboardHelper
        visible: false
    }
}
