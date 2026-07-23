import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// SessionPanel — 左栏：会话管理 + 功能设置。
// 基础骨架：会话列表（占位）+ 模型/工具设置项。
ColumnLayout {
    id: root
    spacing: 0

    // ── 标题区 ──
    ColumnLayout {
        Layout.fillWidth: true
        Layout.margins: Theme.gapLg
        spacing: Theme.gapXs

        Label {
            text: "墨染"
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.sizeDisplay
            font.weight: Font.Bold
            color: Theme.textPrimary
        }
        Label {
            text: bridge.projectName
            font.family: Theme.fontUi
            font.pixelSize: Theme.sizeCaption
            color: Theme.textSecondary
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }

    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

    // ── 会话管理 ──
    ColumnLayout {
        Layout.fillWidth: true
        Layout.margins: Theme.gapMd
        spacing: Theme.gapSm

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "会话"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                font.weight: Font.DemiBold
                color: Theme.textSecondary
                Layout.fillWidth: true
            }
            Button {
                id: newSessionBtn
                text: "+ 新建"
                onClicked: bridge.newSession()
                contentItem: Text {
                    text: newSessionBtn.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeCaption
                    color: Theme.accent
                }
                background: Rectangle {
                    radius: Theme.radiusSm
                    color: newSessionBtn.hovered ? Theme.bgHover : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }
            }
        }

        // 当前会话项（占位，后续扩展为列表）
        Rectangle {
            Layout.fillWidth: true
            height: 40
            radius: Theme.radiusSm
            color: Theme.accentSoft

            RowLayout {
                anchors { fill: parent; leftMargin: Theme.gapMd; rightMargin: Theme.gapMd }
                Rectangle {
                    width: 8; height: 8; radius: 4
                    color: Theme.agentTint
                }
                Label {
                    text: "当前会话"
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeUi
                    color: Theme.textPrimary
                    Layout.fillWidth: true
                }
            }
        }

        Label {
            text: "更多会话管理功能开发中"
            font.family: Theme.fontUi
            font.pixelSize: Theme.sizeCaption
            color: Theme.textFaint
            Layout.alignment: Qt.AlignHCenter
        }
    }

    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

    // ── 功能设置 ──
    ColumnLayout {
        Layout.fillWidth: true
        Layout.margins: Theme.gapMd
        spacing: Theme.gapMd

        Label {
            text: "设置"
            font.family: Theme.fontUi
            font.pixelSize: Theme.sizeCaption
            font.weight: Font.DemiBold
            color: Theme.textSecondary
        }

        // 模型信息
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.gapXs
            Label {
                text: "模型"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textFaint
            }
            Label {
                text: bridge.modelName
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeUi
                color: Theme.textPrimary
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        // Provider 信息
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.gapXs
            Label {
                text: "Provider"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textFaint
            }
            Label {
                text: bridge.providerName
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeUi
                color: Theme.textPrimary
            }
        }    }

    Item { Layout.fillHeight: true }
}
