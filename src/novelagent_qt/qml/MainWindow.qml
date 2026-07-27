import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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

    component WinButton: Button {
        property color hoverColor: Theme.bgHover
        property color hoverText: Theme.textPrimary
        flat: true
        implicitWidth: 36
        implicitHeight: 28
        contentItem: Text {
            text: parent.text
            font.pixelSize: 12
            color: parent.hovered ? parent.hoverText : Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.radiusSm
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
            height: 32
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

                Item { Layout.fillWidth: true }

                WinButton { text: "\u2500"; onClicked: window.showMinimized() }
                WinButton {
                    text: "\u25A1"
                    onClicked: {
                        if (window.visibility === Window.Maximized)
                            window.showNormal()
                        else
                            window.showMaximized()
                    }
                }
                WinButton {
                    text: "\u2715"
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
                implicitWidth: 1
                implicitHeight: 1
                color: Theme.divider
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
    // 否则打开首启向导。
    Component.onCompleted: {
        if (!bridge.tryAutoStart())
            welcomeWizard.open()
    }

    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: window.close()
    }
}
