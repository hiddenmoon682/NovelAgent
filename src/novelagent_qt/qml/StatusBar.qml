import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// StatusBar — 底部状态栏。
// 显示：模型名 | Token 用量 | 上下文百分比 | Provider | 状态文本
Rectangle {
    id: root
    height: 28
    color: Theme.bgSidebar

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
                color: bridge.busy ? Theme.warning : Theme.agentTint
                SequentialAnimation on opacity {
                    running: bridge.busy
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
                color: Theme.bgHover

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

        // Provider · 模型
        Label {
            text: bridge.providerName + " · " + bridge.modelName
            font.family: Theme.fontUi
            font.pixelSize: Theme.sizeCaption
            color: Theme.textFaint
        }
    }

    // 顶部细线分隔
    Rectangle {
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 1
        color: Theme.divider
    }
}
