pragma Singleton
import QtQuick

// Theme — 全局主题常量（深色写作主题）。
// 单一事实来源：所有 QML 组件从此处读取颜色/字体/间距。
QtObject {
    // ── 背景层级（由深到浅，构建空间感）──
    readonly property color bgDeep:      "#0d1117"   // 最深：窗口底色
    readonly property color bgPanel:     "#161b22"   // 面板底色
    readonly property color bgElevated:  "#1c2330"   // 悬浮/输入区
    readonly property color bgHover:     "#232c3d"   // hover 高亮

    // ── 文字 ──
    readonly property color textPrimary:   "#e6edf3"
    readonly property color textSecondary: "#8b949e"
    readonly property color textFaint:     "#5b6570"

    // ── 强调色 ──
    readonly property color accent:        "#4f8cff"   // 主强调（用户消息/按钮）
    readonly property color accentSoft:    "#2d4a7a"   // 弱化强调（边框/选中）
    readonly property color agentTint:     "#3fb98f"   // Agent 标识色
    readonly property color warning:       "#d29922"
    readonly property color danger:        "#f85149"

    // ── 分割线 ──
    readonly property color divider:       "#21262d"

    // ── 字体 ──
    readonly property string fontDisplay:  "Noto Serif SC"   // 标题/正文（衬线）
    readonly property string fontUi:       "Microsoft YaHei UI"  // UI 控件（无衬线）

    // ── 字号 ──
    readonly property int sizeCaption:  11
    readonly property int sizeUi:       13
    readonly property int sizeBody:     15
    readonly property int sizeTitle:    18
    readonly property int sizeDisplay:  22

    // ── 间距 ──
    readonly property int gapXs: 4
    readonly property int gapSm: 8
    readonly property int gapMd: 12
    readonly property int gapLg: 16
    readonly property int gapXl: 24

    // ── 圆角 ──
    readonly property int radiusSm: 6
    readonly property int radiusMd: 10

    // ── 动画时长 ──
    readonly property int animFast: 120
    readonly property int animNormal: 220
}
