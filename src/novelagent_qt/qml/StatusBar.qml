import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// StatusBar — 底部状态栏。
// 显示：模型名 | Token 用量 | 上下文百分比 | Provider | 状态文本
Rectangle {
    id: root
    height: 28
    color: Theme.bgDeep

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

        // 上下文百分比
        Label {
            text: "上下文: " + bridge.contextPercent + "%"
            font.family: Theme.fontUi
            font.pixelSize: Theme.sizeCaption
            color: bridge.contextPercent > 80 ? Theme.warning : Theme.textFaint
        }

        // 模型名
        Label {
            text: bridge.modelName
            font.family: Theme.fontUi
            font.pixelSize: Theme.sizeCaption
            color: Theme.textFaint
        }

        // Provider
        Label {
            text: bridge.providerName
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
