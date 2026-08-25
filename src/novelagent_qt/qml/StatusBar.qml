import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// StatusBar — 底部状态栏。
// 显示：模型名 | Token 用量 | 上下文百分比 | Provider | 状态文本
Rectangle {
    id: root
    height: 28
    color: Theme.bgSidebar

    // 点击 Provider·模型区域时发射，由 MainWindow 打开设置对话框
    signal settingsRequested()

    property string statusText: bridge.statusText

    RowLayout {
        anchors {
            fill: parent
            leftMargin: Theme.gapMd
            rightMargin: Theme.gapMd
        }
        spacing: Theme.gapLg

        // 状态指示
        RowLayout {
            spacing: Theme.gapXs
            Rectangle {
                width: 7; height: 7; radius: 3.5
                color: bridge.sessionBusy ? Theme.warning : Theme.agentTint
                SequentialAnimation on opacity {
                    running: bridge.sessionBusy
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.3; duration: 500 }
                    NumberAnimation { to: 1.0; duration: 500 }
                }
            }
            Label {
                text: root.statusText
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textSecondary
            }
        }

        Item { Layout.fillWidth: true }

        // Token 用量
        Label {
            text: "Tokens: " + bridge.totalTokens
            font.family: Theme.fontUi
            font.pixelSize: Theme.sizeCaption
            color: Theme.textFaint
        }

        // 上下文占用：迷你进度条 + 百分比（>80% 变琥珀警示）
        RowLayout {
            spacing: Theme.gapXs

            Rectangle {
                width: 60
                height: 4
                radius: 2
                color: Theme.divider

                Rectangle {
                    width: parent.width * Math.min(bridge.contextPercent, 100) / 100
                    height: parent.height
                    radius: 2
                    color: bridge.contextPercent > 80 ? Theme.warning : Theme.agentTint
                    Behavior on width { NumberAnimation { duration: Theme.animNormal } }
                }
            }

            Label {
                text: bridge.contextPercent + "%"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: bridge.contextPercent > 80 ? Theme.warning : Theme.textFaint
            }
        }

        // Provider · 模型（点击打开模型设置）
        Label {
            text: (bridge.providerName === "" ? "未配置" : bridge.providerName)
                  + " · " + (bridge.modelName === "" ? "—" : bridge.modelName)
            font.family: Theme.fontUi
            font.pixelSize: Theme.sizeCaption
            color: providerMa.containsMouse ? Theme.textPrimary : Theme.textFaint

            MouseArea {
                id: providerMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.settingsRequested()
            }
        }
    }

    // 顶部细线分隔
    Rectangle {
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 1
        color: Theme.divider
    }
}
