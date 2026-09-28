import QtQuick
import QtQuick.Controls

// 本组件不用 ColumnLayout（原先三行竖直堆叠交给布局管理器）。依据官方 Qt Quick 性能文档对
// delegate 的要求——"The fewer elements that are in a delegate, the faster they can be created,
// and thus the faster the view can be scrolled… use anchors rather than bindings for relative
// positioning within a delegate"：ColumnLayout 会给每个 delegate 额外带来 1 个 QQuickLinearLayout
// 与若干 Layout 附加对象，且每次文本变化（流式逐 token 增长、面板宽度变化）都要跑一整轮布局计算。
// 改为普通 Item + anchors 链；项目规则同样允许 "delegate 行内定位" 使用 anchors。
Item {
    id: root

    property string role: "user"
    property string content: ""
    property string reasoning: ""
    property bool streaming: false
    property bool reasoningExpanded: false
    // 展开态由**父级（AgentPanel）写进 model 条目**，与工具卡片 toolExpanded 同一口径：
    // 本组件只渲染 + 上报点击。理由同 ToolCallCard.qml 注释——delegate 滚出视口会被销毁
    // （reuseItems 为默认 false），组件内属性随之丢失，表现为"思考过程展开后滚走再滚回来
    // 又折叠了"；存进模型才能让每条消息的展开态独立且不随滚动/重建丢失。
    signal expandedToggled
    // 复制请求上报（正文不放本组件内复制）：delegate 里不再各带一个隐藏 TextEdit。
    // 官方 Qt Quick 性能文档的 delegate 建议是"不是立刻需要的东西不要创建"，而 TextEdit
    // 是本组件里最重的一个额外 Item（每个实例都会建一份 QTextDocument + 光标），
    // 于是改为由 AgentPanel 持有一个共享实例（见那里的 copyToClipboard）。
    signal copyRequested(string text)
    // 气泡正文字体：默认取主题「对话正文」档位（无衬线 MiSans，与阅读区衬线分工）。
    // 抽成可覆盖属性是为了能用**真组件**在同一次渲染里并列多款候选字体出对照图
    // （docs/design/previews/chat-font-compare.png），避免手搓仿制品与真实气泡走形。
    property string bodyFont: Theme.fontChat

    width: parent ? parent.width : 0

    // ── 竖直堆叠：anchors 链（替代原 ColumnLayout 的自动排布）──
    // 行间距沿用原 ColumnLayout 的 spacing: gapXs
    readonly property real rowGap: Theme.gapXs
    // 每一行"贴在哪一行下面"（没有上方可见行时贴父项顶部）
    readonly property Item rowAboveBody: reasoningHeaderBox.visible ? reasoningHeaderBox : null
    readonly property Item rowAboveBubble: reasoningBody.visible ? reasoningBody : rowAboveBody
    // 组件总高：取最后一行可见行的底边（供 AgentPanel 的 Loader 读取；
    // 原由 ColumnLayout 的隐式高度承担）
    implicitHeight: bubbleRect.visible ? bubbleRect.y + bubbleRect.height
                  : (reasoningBody.visible ? reasoningBody.y + reasoningBody.height
                                           : (reasoningHeaderBox.visible ? reasoningHeaderBox.y + reasoningHeaderBox.height : 0))

    readonly property bool isUser: role === "user"
    // ── 气泡宽度 ──
    // 本行可用宽度 = root.width（AgentPanel 已在 ListView 上留出 gapLg 左右边距，
    // 那就是"对话列表框的左右间距"）。这里再统一留一个 bubbleMargin，作用是让气泡与
    // 「思考过程 / 工具调用」行的缩进对齐（那两行也是 gapSm 缩进）。
    readonly property real bubbleMargin: Theme.gapSm
    // 气泡宽度上限：
    //   助手气泡 → 占满整行（减掉左右各一个 bubbleMargin），长文本不再只占 82% 而右侧留死白；
    //   用户气泡 → 仍按文本自然宽度收紧、右对齐，上限保留 0.82 行宽（短消息气泡不拉长）。
    readonly property real bubbleLimit: isUser ? width * 0.82
                                               : width - bubbleMargin * 2
    // 气泡正文可用宽度（正文 Text 固定按此宽度排版，理由见 bubbleText.width 处）
    readonly property real bodyMaxWidth: bubbleLimit - Theme.gapMd * 2
    readonly property string displayText: content.replace(/\n{2,}/g, "\n").replace(/\n+$/, "")
    // 去掉尾部换行/空行：若保留，contentHeight 会把末行下方的空行也计入，气泡底部凭空多出空白，
    // 视觉上文本"偏上、下空隙大于上间隙"（与用户消息 displayText 的处理保持一致）。
    // 正文渲染前的显示层归一化链（三步都不改 root.content 原文，复制按钮仍取原文）：
    // 键帽 emoji → 单换行转硬换行 → 中文标点旁的 ** 加粗修复。
    function formatAssistantMarkdown(src) {
        return fixCjkStrong(mdWithHardBreaks(normalizeKeycapEmoji(src))).replace(/\n+$/, "")
    }
    // 分块渲染时整段版不再需要（见 mdFoldCut），置空以免每次 flush 白算一遍 O(整段) 的正则链。
    // ⚠ 因此**分块态下本属性是空串**，只供下方 bubbleText.text 消费；后续若有人要"整段归一化正文"
    // 做别的用途（搜索高亮/导出等），请另加属性，不要复用本属性。
    readonly property string formattedText: isUser ? displayText
                                                   : (mdChunked ? "" : formatAssistantMarkdown(content))

    // ── 流式分块渲染：让"已定稿的前缀"不再随每个 token 重新解析 ──
    // 【依据】qquicktext.cpp（Qt 6.8.3）QQuickText::setText()：文本**与当前值相同**时第 2028 行
    //   直接 return（零代价）；不同时才 updateDocumentText() → extra->doc->setMarkdown(text)
    //   整段重新解析（md4c），随后 updateLayout() → updateSize() 里 doc->size() 强制全文重排。
    //   官方性能文档同源："Calculating text layouts can be a slow operation"。
    //   真机实测（tests/probe_chat_scroll.qml，240Hz 屏一帧仅 4.17ms）：单次写入 500 字 3.1ms /
    //   2000 字 12.3ms / 8000 字 15.7ms / 20000 字 28.9ms —— 逐 token 重排时每个 token 吃掉
    //   3～7 帧，这正是"生成中滚动/阅读时突然卡一下"的来源。
    // 【做法】把正文按**空行块边界**切成「已定稿前缀」+「仍在生长的尾段」两个 Text：
    //   前缀的 text 只在切点前进时才变化 → 其余时间 setText 提前返回，排版缓存整段保留、
    //   场景图里的字形节点也不重建；尾段只解析自己那一段（实测控制在约 320 字 ≈ 2ms）。
    // 【为什么切在空行】CommonMark 里空行是块级分隔符，在空行处切开后前后两段各自解析与整段
    //   解析**逐字等价**——已实测（tests/probe_md_split.qml）：三遍归一化链整段 vs 分段拼接
    //   4/4 完全相等；段落/列表/标题/代码块/引用/长段 8 个样本的排版高度在"接缝 0px"下
    //   逐个精确相等（含递归多接缝）。
    // 【为什么接缝是 0px 而不是别的值】markdown 段落间距来自 qtextmarkdownimporter.cpp：
    //   m_paragraphMargin = 解析时 doc 默认字体.pointSize()*2/3（加在段落块上下边距上），
    //   而 QQuickText 是在 updateSize() 里才 doc->setDefaultFont(自己的字体)（qquicktext.cpp:501），
    //   顺序在解析之后。实测：**首次**解析拿到应用字体 9pt → 6px；**二次解析起** doc 默认字体
    //   已是本 Text 的字体（只设了 pixelSize，pointSize=-1）→ 0px。流式气泡每次 flush 都在重解析，
    //   故其段落间距恒为 0px，前缀 Text 必须同为 0px（见 bubblePrefixText.text 的预热说明）。
    // 【定稿】streaming 结束后退回单 Text 整段渲染（与今天完全一致）：此时 bubbleText 拿到全文
    //   属于"二次解析"，段落间距同样是 0px、接缝同样为 0 → 高度逐像素一致，不会有跳变。
    readonly property int mdFoldMinTail: 320
    readonly property int mdFoldCut: (!isUser && streaming) ? computeFoldCut(content) : 0
    readonly property bool mdChunked: mdFoldCut > 0
    readonly property string mdFrozenMd: mdChunked ? formatAssistantMarkdown(content.substring(0, mdFoldCut)) : ""
    readonly property string mdTailMd: mdChunked ? formatAssistantMarkdown(content.substring(mdFoldCut)) : ""
    // 前缀 Text 是否已"预热"（见 bubblePrefixText.text 处的详细依据）。翻牌放在
    // Component.onCompleted 里：它是本对象自己的信号、在**创建期同步**发出，且必然晚于所有属性
    // 绑定的首次求值 → 首次赋值一定是那个零宽占位符、真实前缀必然是二次解析，与属性赋值顺序无关。
    // 不用 Qt.callLater：本组件会被 ListView 销毁重建，回调可能在对象已销毁后才触发（赋值到
    // 已销毁对象会产生 QML 警告，而本项目要求 QML 警告零容忍）。
    // 实测反例（tests/probe_stream_visual.qml 的块类型矩阵）：一旦让前缀的**首次**解析就吃到
    // 真实内容，前缀内部每条接缝都会多 6px，且在下一次折叠时整体跳一次。
    property bool mdPrefixWarmed: false
    Component.onCompleted: root.mdPrefixWarmed = true

    // 折叠切点（尾段起始下标；0 = 不折叠）。只取"空行块边界"里**最靠后**的一个，
    // 于是切点随内容增长单调前进，且尾段长度保持在 [minTail, minTail+块长)。
    // 全部为纯函数求值，不存任何跨帧状态：delegate 被 ListView 回收复用时不会留下陈旧切点。
    function computeFoldCut(src) {
        var minTail = mdFoldMinTail
        var n = src.length
        if (n < minTail * 2) return 0        // 还太短：折叠省下的解析不足以抵消多一次排版
        var cut = 0
        var fenceCh = ""                     // "" = 不在围栏内；否则为开围栏的字符（` 或 ~）
        var pos = 0
        while (pos < n) {
            var eol = src.indexOf("\n", pos)
            if (eol < 0) break
            var q = pos
            while (q < eol) {                // 行首缩进
                var ch = src.charAt(q)
                if (ch !== " " && ch !== "\t") break
                ++q
            }
            // 围栏行（``` 或 ~~~ 开头）：按"同字符才配对"开关，避免 ``` 内出现 ~~~ 时判错
            var f3 = src.substring(q, q + 3)
            var isFence = (f3 === "```" || f3 === "~~~")
            if (isFence) {
                if (fenceCh === "") fenceCh = src.charAt(q)
                else if (fenceCh === src.charAt(q)) fenceCh = ""
            } else if (fenceCh === "" && q === eol) {
                // 空行 = 块边界候选；切点落在其后第一个非空行的行首
                var j = eol + 1
                while (j < n) {
                    var e2 = src.indexOf("\n", j)
                    if (e2 < 0) e2 = n
                    if (e2 > j) break
                    j = e2 + 1
                }
                if (j < n && n - j >= minTail && isSafeTailStart(src, j, n)) cut = j
            }
            pos = eol + 1
        }
        return cut
    }

    // 切点后一行必须是"自成一块、不依赖上一块"的行。列表项与引用行会被切开后改变渲染：
    // "- 甲\n\n- 乙" 在 CommonMark 里是**同一个松散列表**，切开会让后半段的编号/项目符号重新开始；
    // 缩进行则是上一块的续行/缩进代码块。这些位置一律放弃折叠（退化为整段在尾段里渲染）。
    function isSafeTailStart(src, j, n) {
        var e = src.indexOf("\n", j)
        if (e < 0) e = n
        var line = src.substring(j, e)
        var c = line.charAt(0)
        if (c === " " || c === "\t") return false
        if (c === "-" || c === "*" || c === "+" || c === ">") return false
        if (c >= "0" && c <= "9") {
            var k = 1
            while (k < line.length && line.charAt(k) >= "0" && line.charAt(k) <= "9") ++k
            if (line.charAt(k) === "." || line.charAt(k) === ")") return false
        }
        return true
    }
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

    // ── 中文语境的 ** 加粗修复（CommonMark flanking 规则对 CJK 不友好）──
    // CommonMark 规定：闭合 ** 必须"右侧定界符合法"——其前一个字符不能是标点，除非其后一个
    // 字符是空白或标点。中文里没有词间空格，于是「…（或魔幻现实）**的路子」这种写法里，
    // 闭合 ** 前是全角右括号、后是汉字，两条都不满足 → 该 ** 无资格闭合强调，
    // 解析器把它当普通文本输出（用户可见"星号外露"）。开标记后紧跟标点时间理，如
    // 「氛围是**「悬疑」**的路子」。这是 CommonMark 的已知边界（commonmark-spec#650，
    // 2020 年至今未修），ChatGPT、OpenClaw 等同样中招（OpenAI 社区有中/日/韩文报告）。
    // 修法：在成对 ** 的**内侧**各插一个 U+2060 WORD JOINER——零宽字符，且不像 U+200B
    // 那样产生断行机会，使两侧重新成为合法定界符。效果与上游 CJK-friendly 修订草案
    // （把 flanking 判定里的"标点"收窄为"非 CJK 标点"）等价；Qt 的 markdown 是 md4c
    // 移植版、没有插件挂载点，只能落在渲染前这一层。
    // 边界处理：
    //   ① 只处理**内侧紧贴非空白**的成对 **：`** 加粗 **`（内边带空格）在 CommonMark 里
    //      本就是字面量，保持原样不动，避免改动超出必要范围。
    //   ② 跳过 ****粗****（加粗+斜体）这类相邻星号：其前/后紧邻 * 时不处理。
    //   ③ 代码块（```…```）内不处理：那里的 ** 本就该是字面量。
    //   ④ 单 * 斜体与下划线 _ 不处理：中文输出罕见，且 _ 与 story_structure 这类标识符冲突风险高。
    function fixCjkStrong(src) {
        var wj = "\u2060"
        var parts = src.split("```")
        for (var i = 0; i < parts.length; i += 2) {   // 偶数段在代码块外
            parts[i] = parts[i].replace(/\*\*([^\s\n*](?:[^\n*]*[^\s\n*])?)\*\*/g,
                function (m, inner, offset, whole) {
                    var prev = offset > 0 ? whole.charAt(offset - 1) : ""
                    var next = whole.charAt(offset + m.length)
                    if (prev === "*" || next === "*") return m
                    return "**" + wj + inner + wj + "**"
                })
        }
        return parts.join("```")
    }

    // ── 思考过程折叠条（仅 assistant 且 reasoning 非空）──
    Rectangle {
        id: reasoningHeaderBox
        visible: !root.isUser && root.reasoning.length > 0
        anchors.left: parent.left
        anchors.leftMargin: Theme.gapSm
        anchors.top: parent.top
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
            onClicked: root.expandedToggled()   // 状态归父级写回模型，见 expandedToggled 声明处
        }
    }

    // ── 展开的思考过程正文（左侧竖线 + 弱化小字）──
    // 折叠态不做任何思考过程文本工作：这里的 height 绑定会强制 Text 排版，
    // 而 visible:false 并不阻止绑定求值——若 text 一直指向全文，每条历史消息（含切换
    // 会话重建的全部 delegate、reasoning 可达数千字符）都会白排版一次。故 text 只在
    // 展开时求值（归一化也一并延后），折叠态为空串。
    Rectangle {
        id: reasoningBody
        visible: !root.isUser && root.reasoningExpanded && root.reasoning.length > 0
        // 与助手气泡同宽同边距（占满整行），使思考过程正文框的左右边缘与下方气泡对齐；
        // 此前固定 0.82 行宽，与加宽后的气泡不一致。
        anchors.top: root.rowAboveBody ? root.rowAboveBody.bottom : parent.top
        anchors.topMargin: root.rowAboveBody ? root.rowGap : 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.bubbleMargin
        anchors.rightMargin: root.bubbleMargin
        height: reasoningText.implicitHeight + Theme.gapSm * 2
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
        anchors.top: root.rowAboveBubble ? root.rowAboveBubble.bottom : parent.top
        anchors.topMargin: root.rowAboveBubble ? root.rowGap : 0
        // 对齐：统一锚右边 → 用户气泡右对齐；助手气泡宽度 = 行宽 - 左右各一个 bubbleMargin，
        // 左边缘因此自然落在 bubbleMargin 处（与旧 Layout.alignment/margins 的观感一致）。
        anchors.right: parent.right
        anchors.rightMargin: root.bubbleMargin

        // 气泡宽度：
        //   助手 → 恒等于整行减左右各一个 bubbleMargin（占满整行），不再按文本收紧；
        //   用户 → 按文本自然宽度收紧：此前用 Math.max(40, …) 做最小文本宽，
        //          短消息（如"你好"，文本仅 ~30px）会被强撑到 40px 文本区→气泡约 64px，
        //          实际文本只占左侧、右侧多出一段死白。改用较小地板（留出 24px 保底），
        //          使气泡贴合文本、两侧留白对称。
        //
        // 用户侧的宽度来源是**已排版正文的 contentWidth**（不再用额外的隐藏测量 Text）：
        // 正文 Text 以固定上限宽度排版（见下 width: root.bodyMaxWidth），因此 contentWidth 就是
        // "不超过上限时文本的真实宽度"——单行短文本 = 自然宽度，换行长文本 = 上限。
        // 这样既省掉"把整段正文再排版一遍"的隐藏 Text（实测占会话切换 delegate 创建
        // 耗时的一半以上），也不会出现 bubbleText.width ↔ bubbleRect.width 的绑定环
        //（正文宽度只依赖面板宽度 root.width，与气泡宽度无关）。
        width: root.isUser
               ? Math.min(Math.max(Theme.gapMd * 2, bubbleText.contentWidth) + Theme.gapMd * 2,
                          root.bubbleLimit)
               : Math.max(root.width - root.bubbleMargin * 2, 0)
        // 上下内边距用 gapSm(8) 而非 gapXs(4)：4px 时文字几乎贴着上下边框（实测顶边到首行墨迹
        // 仅约 5 逻辑px），与左右 12px 的内边距相比明显偏紧。8/12 的组合是聊天气泡的常见比例
        // （文字块的行框本身已含约 2.4px 上下行距，视觉留白≈10px 对 12px，观感均衡）。
        height: bubbleText.contentHeight + Theme.gapSm * 2
                + (bubblePrefixText.visible ? bubblePrefixText.contentHeight : 0)
        radius: Theme.radiusMd
        color: root.isUser ? Theme.accentSoft : Theme.bgElevated
        border.width: root.isUser ? 0 : 1
        border.color: Theme.divider

        // ── 流式分块：已定稿前缀（只在本组件处于分块态时可见）──
        // 位置在正文上方，两个 Text 之间**不留间距**：接缝处就是 markdown 的块边界，
        // 而流式期间的段落间距实测为 0px（见 mdFoldCut 处依据），故 spacing 必须也是 0。
        Text {
            id: bubblePrefixText
            x: Theme.gapMd
            y: Theme.gapSm
            width: root.bodyMaxWidth
            visible: root.mdChunked
            wrapMode: Text.Wrap
            textFormat: Text.MarkdownText
            font.family: root.bodyFont
            font.pixelSize: Theme.sizeBody
            color: Theme.textPrimary
            linkColor: Theme.accent
            padding: 0
            // 未预热时喂一个 U+2060 WORD JOINER（零宽、不可见）而不是空串：用途是**预热**。
            // markdown 段落边距取的是"解析时 doc 默认字体的 pointSize*2/3"
            // （qtextmarkdownimporter.cpp），而 QQuickText 是在 updateSize() 里才
            // doc->setDefaultFont(本 Text 的字体)（qquicktext.cpp:501），顺序在解析之后；
            // 于是**首次**解析拿到应用字体 9pt → 6px，**二次解析起**才拿到本 Text 的字体
            // （只设了 pixelSize，pointSize=-1）→ 0px（实测见 tests/probe_md_split.qml）。
            // 若让真实前缀去做那次"首次解析"，它的**每一条内部接缝**都会是 6px，与单 Text 渲染
            // （0px）不符，且会在下一次折叠时整体跳一次；块类型矩阵实测：前缀含 2 条接缝时偏差 +12px
            // （tests/probe_stream_visual.qml）。故用零宽字符把首次解析提前用掉（它在
            // mdChunked 为真之前 `visible: false`，不会闪现）。
            text: root.mdPrefixWarmed ? (root.mdChunked ? root.mdFrozenMd : "") : "\u2060"
        }

        Text {
            id: bubbleText
            x: Theme.gapMd
            // 分块态下让出前缀占用的高度（两者共用一个上内边距，见 bubbleRect.height）
            y: Theme.gapSm + (bubblePrefixText.visible ? bubblePrefixText.contentHeight : 0)
            // 固定为上限宽度（不跟随气泡宽度）：这是上面 width 直接取 contentWidth
            // 的前提——宽度依赖面板而非气泡，既无绑定环，也无需第二次排版来测自然宽度。
            width: root.bodyMaxWidth
            // 分块态只渲染尾段（前缀由 bubblePrefixText 承担），定稿后退回整段
            text: (root.mdChunked ? root.mdTailMd : root.formattedText)
                  + (!root.isUser && root.streaming ? "▍" : "")
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
            // 原先这里挂着一个 `SequentialAnimation on color { running: root.streaming }` 做"逐字
            // 光标呼吸"。它每帧都在改 Text 的 color → 每帧重绘整段正文（助手回复可达数千字符），
            // 且每个 delegate 都带 3 个动画对象；官方性能文档另有"Avoid running JavaScript during
            // animation / 动画会触发绑定重算"的告诫。改为静态光标（见上面的 "▍"），流式期间不再
            // 有整段重绘。
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
                    // 复制动作交给面板级共享 TextEdit（见 copyRequested 声明处的说明）
                    root.copyRequested(root.content)
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
    }
}
