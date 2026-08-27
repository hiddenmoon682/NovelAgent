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

    // ── 选中标条 ──
    readonly property real markBar: 2.5     // 朱砂选中标条宽度（原型 2.5px）

    // ── 动画时长 ──
    readonly property int animFast: 120
    readonly property int animNormal: 220
}
