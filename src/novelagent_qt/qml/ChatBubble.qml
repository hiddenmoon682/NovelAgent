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
    // 气泡正文字体：默认取主题「对话正文」档位（无衬线 MiSans，与阅读区衬线分工）。
    // 抽成可覆盖属性是为了能用**真组件**在同一次渲染里并列多款候选字体出对照图
    // （docs/design/previews/chat-font-compare.png），避免手搓仿制品与真实气泡走形。
    property string bodyFont: Theme.fontChat

    width: parent ? parent.width : 0
    spacing: Theme.gapXs

    readonly property bool isUser: role === "user"
    readonly property string displayText: content.replace(/\n{2,}/g, "\n").replace(/\n+$/, "")
    // 去掉尾部换行/空行：若保留，contentHeight 会把末行下方的空行也计入，气泡底部凭空多出空白，
    // 视觉上文本"偏上、下空隙大于上间隙"（与用户消息 displayText 的处理保持一致）。
    readonly property string formattedText: isUser ? displayText : mdWithHardBreaks(normalizeKeycapEmoji(content)).replace(/\n+$/, "")
    // 与消息正文一致：去掉尾部换行/空行，避免把末行下方的空行也计入高度，
    // 使思考过程框只包裹可见文本（此前 reasoning 直接 text: root.reasoning，尾部换行会让框变高、文本偏上）；
    // 键帽 emoji 与正文同一渲染机制，同样做归一化（见 normalizeKeycapEmoji 注释）。
    // 注意：思考过程正文**不再预做成 property**——属性绑定是无条件求值的，会让每条消息的
    // 正则归一化（reasoning 可达数千字符）在折叠态也照样执行；改为在展开态 Text 的绑定里内联
    // 求值（见下方 reasoningText.text）。

    // 键帽 emoji（1️⃣ 2️⃣ 🔟 #️⃣ *️⃣）由"数字/符号 + U+FE0F 变体选择符 + U+20E3 组合键帽框"组成，
    // 衬线主题字体（Noto Serif SC）只有基础字形：数字正常、键帽框字形缺失，
    // 回退字体嵌入的键帽框与数字叠加错位，视觉上表现为"裸数字/数字套碎框"（1️⃣→"1"）。
    // 修复：显示层归一化为纯文本标记（1️⃣→"1."、🔟→"10."、#️⃣→"#"），与暖墨单色主题一致；
    // 复制按钮取原始 content，不丢模型原文。兼容含/不含 U+FE0F 两种写法（"1️⃣"与"1⃣"）。
    function normalizeKeycapEmoji(src) {
        var s = src.replace(/[0-9]\uFE0F?\u20E3/g, function(m) { return m.charAt(0) + "." })
        s = s.replace(/\uD83D\uDD1F/g, "10.")                       // 🔟 → "10."
        s = s.replace(/[#*]\uFE0F?\u20E3/g, function(m) { return m.charAt(0) })
        return s
    }

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
    // 折叠态不做任何思考过程文本工作：这里的 implicitHeight 绑定会强制 Text 排版，
    // 而 visible:false 并不阻止绑定求值——若 text 一直指向全文，每条历史消息（含切换
    // 会话重建的全部 delegate、reasoning 可达数千字符）都会白排版一次。故 text 只在
    // 展开时求值（归一化也一并延后），折叠态为空串。
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
            text: root.reasoningExpanded
                  ? root.normalizeKeycapEmoji(root.reasoning).replace(/\n+$/, "")
                  : ""
            wrapMode: Text.Wrap
            font.family: Theme.fontUi
            font.pixelSize: Theme.sizeCaption + 1
            // 与消息气泡一致：用字体自然行高（不再额外 1.5 放大），
            // 使浅色小字在思考过程框内上下居中、下方不再多出空白。
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
        //
        // 宽度来源改为**已排版正文的 contentWidth**（不再用额外的隐藏测量 Text）：
        // 正文 Text 以固定上限宽度排版（见下 width: maxWidth），因此 contentWidth 就是
        // "不超过上限时文本的真实宽度"——单行短文本 = 自然宽度，换行长文本 = 上限。
        // 这样既省掉"把整段正文再排版一遍"的隐藏 Text（实测占会话切换 delegate 创建
        // 耗时的一半以上），也不会出现 bubbleText.width ↔ bubbleRect.width 的绑定环
        //（正文宽度只依赖面板宽度 root.width，与气泡宽度无关）。
        implicitWidth: Math.min(bubbleText.maxWidth, Math.max(Theme.gapMd * 2, bubbleText.contentWidth)) + Theme.gapMd * 2
        implicitHeight: bubbleText.contentHeight + Theme.gapXs * 2
        radius: Theme.radiusMd
        color: root.isUser ? Theme.accentSoft : Theme.bgElevated
        border.width: root.isUser ? 0 : 1
        border.color: Theme.divider

        Text {
            id: bubbleText
            x: Theme.gapMd
            y: Theme.gapXs
            property real maxWidth: root.width * 0.82 - Theme.gapMd * 2
            // 固定为上限宽度（不跟随气泡宽度）：这是上面 implicitWidth 直接取 contentWidth
            // 的前提——宽度依赖面板而非气泡，既无绑定环，也无需第二次排版来测自然宽度。
            width: maxWidth
            text: root.formattedText + (!root.isUser && root.streaming ? "▍" : "")
            font.family: root.bodyFont
            font.pixelSize: Theme.sizeBody
            // 用字体自然行高，别再额外放大（1.4 / FixedHeight20）：Noto Serif SC 15px 自然行高约 22px
            // 已含充分的 CJK 行距；再放大到 ~31px 会把约 13px 的字形挤到行框顶部、下方多出大片空白，
            // 视觉上"文本偏上、下空隙大于上空隙"。改用默认行高后文本在气泡内上下居中。
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
