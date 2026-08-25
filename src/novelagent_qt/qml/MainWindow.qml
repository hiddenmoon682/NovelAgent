import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

ApplicationWindow {
    id: window
    width: 1400
    height: 900
    minimumWidth: 1000
    minimumHeight: 600
    visible: true
    title: "墨染 · AI小说创作助手"
    flags: Qt.FramelessWindowHint | Qt.Window

    color: Theme.bgSidebar

    Material.theme: Material.Dark
    Material.accent: Theme.accent

    // 模态遮罩统一深色半透明：Material 默认遮罩偏浅色，与暖墨深色主题冲突。
    Overlay.modal: Rectangle { color: "#99000000" }
    Overlay.modeless: Rectangle { color: "transparent" }

    // 窗口几何持久化。不直接用别名绑定 window.x/width：最大化退出时这些值会
    // 变成整屏几何，下次启动按它恢复成"满屏假最大化"（历史 bug 根因），普通态
    // 也会被整屏值污染。改为显式属性、关闭时保存；键名自旧别名键重命名，
    // 旧键里的越界残留值因此一次性作废，回落默认窗口尺寸
    Settings {
        id: winState
        category: "MainWindow"
        property int savedX: -1
        property int savedY: -1
        property int savedWidth: 1400
        property int savedHeight: 900
        property bool savedMaximized: false
    }

    // 还原持久化几何：尺寸越界（残留整屏/换屏/首启缺省）回落默认尺寸；
    // 位置不可见（负坐标哨兵/不在当前屏内/多屏摘除）回落居中。Component.onCompleted
    // 在窗口映射前执行，几何调整不会产生可见跳动
    function restoreGeometry() {
        const scr = window.screen
        if (!scr || !scr.availableGeometry)
            return
        const av = scr.availableGeometry
        let w = winState.savedWidth
        let h = winState.savedHeight
        const sizeValid = w >= minimumWidth && h >= minimumHeight &&
                          w < av.width && h < av.height
        if (!sizeValid) {
            w = Math.min(1400, av.width)
            h = Math.min(900, av.height)
        }
        let px = winState.savedX
        let py = winState.savedY
        const havePos = px >= 0 && py >= 0
        const posVisible = havePos && px + w > av.x && px < av.x + av.width &&
                           py + h > av.y && py < av.y + av.height
        if (!sizeValid || !posVisible) {
            px = av.x + Math.round((av.width - w) / 2)
            py = av.y + Math.round((av.height - h) / 2)
        }
        window.x = px
        window.y = py
        window.width = w
        window.height = h

        if (winState.savedMaximized)
            window.showMaximized()
    }

    onClosing: {
        // 最大化退出只记状态、不覆盖普通几何，避免整屏几何污染下次启动
        if (window.visibility === Window.Maximized) {
            winState.savedMaximized = true
            winState.sync()
            return
        }
        winState.savedMaximized = false
        winState.savedX = window.x
        winState.savedY = window.y
        winState.savedWidth = window.width
        winState.savedHeight = window.height
        winState.sync()
    }

    // 窗口控制按钮：Segoe MDL2 Assets 为 Windows 系统图标字体，
    // 三枚按钮图标同源同尺寸，与原生标题栏观感一致
    component WinButton: Button {
        property color hoverColor: Theme.bgHover
        property color hoverText: Theme.textPrimary
        flat: true
        // Material 样式默认 top/bottomInset=6，会把 background 布局内缩，
        // 此处归零使悬浮高亮真正铺满按钮矩形
        topInset: 0
        bottomInset: 0
        width: 46
        height: parent ? parent.height : 40
        contentItem: Text {
            text: parent.text
            font.family: "Segoe MDL2 Assets"
            font.pixelSize: 12
            color: parent.hovered ? parent.hoverText : Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: parent.hovered ? parent.hoverColor : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.animFast } }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── 自定义标题拖拽区 ──
        Rectangle {
            Layout.fillWidth: true
            height: 40
            color: Theme.bgSidebar

            MouseArea {
                anchors.fill: parent
                property point clickPos: Qt.point(0, 0)
                onPressed: (mouse) => { clickPos = Qt.point(mouse.x, mouse.y) }
                onPositionChanged: (mouse) => {
                    var delta = Qt.point(mouse.x - clickPos.x, mouse.y - clickPos.y)
                    window.setX(window.x + delta.x)
                    window.setY(window.y + delta.y)
                }
                onDoubleClicked: {
                    if (window.visibility === Window.Maximized)
                        window.showNormal()
                    else
                        window.showMaximized()
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.gapMd
                anchors.rightMargin: Theme.gapSm

                // 左上角弱化小字作窗口定位，艺术字风格留给正文/对话框场景，避免抢视觉焦点
                Label {
                    text: "墨染 · AI 小说创作助手"
                    font.family: Theme.fontUi
                    font.pixelSize: 14
                    color: Theme.textFaint
                }

                Item { Layout.fillWidth: true }
            }

            // 标题栏底部分割线：标题栏与侧边栏同底色，左上无边界线导致下方
            // 卡片"距标题栏"的间隙无可参照边界（感知大于实际）；补线后上下对称
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: Theme.divider
            }

            // 窗口控制按钮：右对齐并锚定标题栏上下边，悬浮高亮全高无间隙；
            // 贴右缘（无 margin），叉号悬停在窗口最右上角
            Row {
                anchors { top: parent.top; bottom: parent.bottom; right: parent.right }

                WinButton {
                    text: "\uE921"  // U+E921 Minimize
                    onClicked: window.showMinimized()
                }
                WinButton {
                    text: window.visibility === Window.Maximized ? "\uE923" : "\uE922"  // E923 Restore / E922 Maximize
                    onClicked: {
                        if (window.visibility === Window.Maximized)
                            window.showNormal()
                        else
                            window.showMaximized()
                    }
                }
                WinButton {
                    text: "\uE8BB"  // U+E8BB Close
                    hoverColor: Theme.accent
                    hoverText: "#f5efe2"
                    onClicked: window.close()
                }
            }
        }

        // ── 主体三栏布局 ──
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            handle: Rectangle {
                implicitWidth: 3
                implicitHeight: 3
                color: "transparent"

                // 视觉仍为 1px 细线，但命中区扩到 3px 且 hover 高亮，提示可拖拽
                Rectangle {
                    anchors.centerIn: parent
                    width: 1
                    height: parent.height
                    color: SplitHandle.hovered || SplitHandle.pressed
                           ? Theme.textFaint : Theme.divider
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }
            }

            SidebarPanel {
                SplitView.preferredWidth: 240
                SplitView.minimumWidth: 200
                SplitView.maximumWidth: 320
                onSettingsRequested: settingsDialog.openAt(0)
            }

            AgentPanel {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 400
            }

            ReaderPanel {
                id: readerPanel
                SplitView.preferredWidth: 420
                SplitView.minimumWidth: 300
            }
        }
    }

    footer: StatusBar {
        onSettingsRequested: settingsDialog.openAt(0)
    }

    SettingsDialog { id: settingsDialog }
    WelcomeWizard { id: welcomeWizard }

    // 启动策略：有默认 Provider + 有效 Key → 自动初始化（并恢复上次项目）；
    // 否则打开首启向导。先恢复窗口几何（onCompleted 在窗口映射前执行，无跳动）
    Component.onCompleted: {
        restoreGeometry()
        if (!bridge.tryAutoStart())
            welcomeWizard.open()
    }

    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: window.close()
    }
}
