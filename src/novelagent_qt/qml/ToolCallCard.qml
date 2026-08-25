import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ToolCallCard — 工具调用状态卡片（居左窄条）。
// status: "running"（⚙ 旋转）| "ok"（✓ 青竹）| "error"（✕ danger）
Item {
    id: root

    property string toolName: ""
    property string status: "running"

    implicitHeight: card.height

    onStatusChanged: if (status !== "running") icon.rotation = 0

    Rectangle {
        id: card
        width: row.implicitWidth + Theme.gapMd * 2
        height: 30
        radius: Theme.radiusSm
        color: Theme.bgElevated
        border.width: 1
        border.color: root.status === "error" ? Theme.danger : Theme.divider

        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: Theme.gapSm

            Label {
                id: icon
                // \uFE0E 强制文本呈现，避免 Windows 把 ⚙ 渲染成彩色 Emoji
                text: root.status === "running" ? "\u2699\uFE0E"
                    : root.status === "ok" ? "\u2713" : "\u2715"
                font.pixelSize: Theme.sizeUi
                color: root.status === "error" ? Theme.danger : Theme.agentTint

                RotationAnimation on rotation {
                    running: root.status === "running"
                    from: 0; to: 360
                    duration: 1600
                    loops: Animation.Infinite
                }
            }

            Label {
                text: root.toolName + (root.status === "running" ? " · 执行中…"
                    : root.status === "ok" ? " · 完成" : " · 失败")
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeUi
                color: root.status === "error" ? Theme.danger : Theme.textSecondary
            }
        }
    }
}
