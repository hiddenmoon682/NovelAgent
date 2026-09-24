pragma Singleton
import QtQuick

// Theme — 全局主题常量（「墨染书房」暖墨主题）。
// 单一事实来源：所有 QML 组件从此处读取颜色/字体/间距。
QtObject {
    // ── 背景层级（按栏位语义化，左深右浅突出对话区）──
    readonly property color bgSidebar:   "#231f1a"   // 左侧栏（最深）
    readonly property color bgChat:      "#322c24"   // 对话区（最浅，视觉焦点）
    readonly property color bgReader:    "#2a251e"   // 阅读区
    readonly property color bgElevated:  "#3d362b"   // 输入框/悬浮卡片
    readonly property color bgField:     "#2b251d"   // 表单字段内嵌底（比卡片深，呈凹陷感）
    readonly property color bgHover:     "#4a4234"   // hover 高亮

    // ── 文字 ──
    readonly property color textPrimary:   "#f0eadd"   // 宣纸色
    readonly property color textSecondary: "#aca293"
    readonly property color textFaint:     "#7f7565"

    // ── 强调色 ──
    readonly property color accent:        "#c9553e"   // 朱砂：按钮/选中/用户标识
    readonly property color accentSoft:    "#8c3f2e"   // 用户消息气泡底
    readonly property color accentTint:    "#1AC9553E" // 选中行半透明朱砂底（原型 rgba(201,85,62,.10)）
    readonly property color agentTint:     "#a3b48a"   // 青竹：Agent 标识/工具卡片
    readonly property color warning:       "#d4a373"   // 琥珀警示
    readonly property color danger:        "#c0392b"

    // ── 分割线 ──
    readonly property color divider:       "#453d30"

    // ── 弹窗遮罩 ──
    readonly property color overlayDim: "#99000000"   // 模态遮罩 60% 黑（覆盖 Material 默认偏浅遮罩）

    // ── 字体 ──
    readonly property string fontDisplay:  "Noto Serif SC"       // 标题/正文（衬线）
    readonly property string fontUi:       "Microsoft YaHei UI"  // UI 控件（无衬线）
    // 对话列表正文（气泡内消息）：无衬线，与阅读区衬线明确分工——
    // 阅读区是要"读进去"的小说正文，用衬线（fontDisplay）；对话列表是交互内容，
    // 无衬线在深色底上笔画更实、小字号更清晰（现状衬线在 15px 深色底上细横画发虚）。
    // 选型依据：docs/design/previews/chat-font-compare.png（真气泡实况对照）+
    // chat-font-glyph-detail.png（110px 字形细节）+ FontMetrics 度量（行高 19.9px/行）。
    readonly property string fontChat:     "MiSans"              // 对话正文（无衬线）
    // 图标字体（Windows 系统图标字体，字形在 Unicode 私用区 PUA）：
    // \uE700 ☰ 目录 / \uE8BB ✕ 关闭 / \uE70D ⌄ 下拉 / \uE74D 垃圾桶 / \uE921-23 窗口按钮。
    // 必须用本档位渲染，不得用 fontUi——PUA 字形只在该字体中存在，用普通文本字体会
    // 渲染成空白（无警告、无 fallback），历史上 ☰ 与 ✕ 两个图标即因此"整体消失"。
    readonly property string fontIcon:     "Segoe MDL2 Assets"

    // ── 字号 ──
    readonly property int sizeMini:     10   // "默认"/"当前"徽标
    readonly property int sizeCaption:  11
    readonly property int sizeNote:     12   // 卡片内小标签（介于 caption 与 ui 之间）
    readonly property int sizeUi:       13
    readonly property int sizeBody:     15
    readonly property int sizeTitle:    18
    readonly property int sizeDisplay:  22
    readonly property int sizeHero:     34   // 空态/欢迎大字标题（原 sizeDisplay+12 内联值收敛为档位）

    // ── 间距（含原型细档位：micro 2 / tight 6 / cozy 10 / relaxed 14 / spacious 18 / ample 20）──
    readonly property int gapMicro:    2
    readonly property int gapXs:       4
    readonly property int gapTight:    6
    readonly property int gapSm:       8
    readonly property int gapCozy:     10
    readonly property int gapMd:       12
    readonly property int gapRelaxed:  14
    readonly property int gapLg:       16
    readonly property int gapSpacious: 18
    readonly property int gapAmple:    20
    readonly property int gapXl:       24

    // ── 圆角 ──
    readonly property int radiusXs:    4    // 徽标/垃圾桶 chip
    readonly property int radiusSm:    6
    readonly property int radiusMd:    10
    readonly property int radiusToast: 8    // toast 提示

    // ── 阅读区行距 ──
    // 正文行距，**像素值**（ReaderPanel 以 lineHeightMode: Text.FixedHeight 生效）。
    // 为什么必须用固定像素而非倍数：QML 的 Text.ProportionalHeight 把 lineHeight 乘在
    // **字体自身行盒**上而不是字号——Noto Serif SC 在 16px 字号下字体行盒 23px，故
    // lineHeight: 1.3 实得 30px/行（= 1.875 倍字号，比原型 HTML 的 line-height:1.3
    // 语义高出 44%），"两行之间的高度"明显偏空（用户反馈）。
    // 24px/行 = 1.5 倍字号，主流阅读器行高区间的下沿；对照图
    // docs/design/previews/reader-line-height-compare.png（30 / 27 / 24 / 22 四档实测）。
    readonly property int readerLineHeight: 24

    // ── 阅读区段距 ──
    // 章节正文段落空隙（固定像素，与行距 readerLineHeight 相互独立）。
    // 正文每段一个 ListView 委托项，段距直接叠加在行盒之上：24px 行盒下，段间比段内
    // 多 8px 空白（约 2 倍分隔度），既有段落分隔感又不空行。
    // 取值过程见 docs/design/previews/paragraph-spacing-compare.png
    // （16 / 8 / 4 / 0+首行缩进 四档对照，16px 那档即用户反馈"间距有点大"的原状）。
    readonly property int readerParagraphGap: 8

    // ── 阅读区尺寸 ──
    readonly property int readerDrawerWidth: 300  // 目录抽屉宽度（方案 B；尺寸档位，禁内联魔法值）

    // ── 选中标条 ──
    readonly property real markBar: 2.5     // 朱砂选中标条宽度（原型 2.5px）

    // ── 动画时长 ──
    readonly property int animFast: 120
    readonly property int animNormal: 220
}
