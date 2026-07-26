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

    color: Theme.bgDeep

    Material.theme: Material.Dark
    Material.accent: Theme.accent

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── 自定义标题拖拽区 ──
        Rectangle {
            Layout.fillWidth: true
            height: 32
            color: Theme.bgDeep

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

                Button {
                    text: "\u2500"
                    flat: true
                    onClicked: window.showMinimized()
                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: 12
                        color: Theme.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    implicitWidth: 36
                    implicitHeight: 28
                }
                Button {
                    text: "\u25A1"
                    flat: true
                    onClicked: {
                        if (window.visibility === Window.Maximized)
                            window.showNormal()
                        else
                            window.showMaximized()
                    }
                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: 12
                        color: Theme.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    implicitWidth: 36
                    implicitHeight: 28
                }
                Button {
                    text: "\u2715"
                    flat: true
                    onClicked: window.close()
                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: 12
                        color: Theme.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    implicitWidth: 36
                    implicitHeight: 28
                }
            }
        }

        // ── 主体四栏布局 ──
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            ProjectPanel {
                SplitView.preferredWidth: 200
                SplitView.minimumWidth: 160
                SplitView.maximumWidth: 280
            }

            SessionPanel {
                SplitView.preferredWidth: 220
                SplitView.minimumWidth: 180
                SplitView.maximumWidth: 300
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

    footer: StatusBar {}

    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: window.close()
    }
}
