import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// SidebarPanel — 左侧栏：应用标题 + 当前项目卡片 + 会话列表 + 设置入口。
Rectangle {
    id: root
    color: Theme.bgSidebar

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── 应用标题 ──
        Label {
            Layout.leftMargin: Theme.gapLg
            Layout.topMargin: Theme.gapLg
            Layout.bottomMargin: Theme.gapMd
            text: "墨染"
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.sizeDisplay
            font.weight: Font.Bold
            color: Theme.textPrimary
        }

        // ── 当前项目卡片 ──
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapSm
            Layout.rightMargin: Theme.gapSm
            Layout.bottomMargin: Theme.gapMd
            height: 56
            radius: Theme.radiusMd
            color: Theme.bgElevated
            border.width: 1
            border.color: Theme.divider

            ColumnLayout {
                anchors { fill: parent; leftMargin: Theme.gapMd; rightMargin: Theme.gapMd }
                spacing: 2
                Label {
                    text: "当前项目"
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeCaption
                    color: Theme.textFaint
                }
                Label {
                    text: bridge.projectName
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.sizeUi
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

        // ── 会话标题行 ──
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapLg
            Layout.rightMargin: Theme.gapSm
            Layout.topMargin: Theme.gapMd
            Layout.bottomMargin: Theme.gapSm

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

        // ── 会话列表 ──
        ListView {
            id: sessionList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            leftMargin: Theme.gapSm
            rightMargin: Theme.gapSm
            spacing: 2

            model: ListModel {
                ListElement { name: "当前会话"; active: true }
            }

            delegate: Rectangle {
                width: sessionList.width - Theme.gapSm * 2
                height: 36
                radius: Theme.radiusSm
                color: model.active ? Theme.bgHover
                     : sessionMa.containsMouse ? Theme.bgHover : "transparent"

                RowLayout {
                    anchors { fill: parent; leftMargin: Theme.gapMd; rightMargin: Theme.gapMd }
                    spacing: Theme.gapSm
                    Rectangle {
                        width: 7; height: 7; radius: 3.5
                        color: model.active ? Theme.agentTint : Theme.textFaint
                    }
                    Label {
                        text: model.name
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.sizeUi
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                MouseArea {
                    id: sessionMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

        // ── 底部设置入口 ──
        Rectangle {
            Layout.fillWidth: true
            height: 44
            color: "transparent"

            Rectangle {
                anchors { right: parent.right; rightMargin: Theme.gapMd; verticalCenter: parent.verticalCenter }
                width: 32
                height: 32
                radius: Theme.radiusSm
                color: settingsMa.containsMouse ? Theme.bgHover : "transparent"

                ToolTip.visible: settingsMa.containsMouse
                ToolTip.text: "设置功能开发中"
                ToolTip.delay: 300

                Label {
                    anchors.centerIn: parent
                    text: "\u2699"
                    font.pixelSize: 18
                    color: settingsMa.containsMouse ? Theme.textPrimary : Theme.textSecondary
                }

                MouseArea {
                    id: settingsMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }
    }
}
