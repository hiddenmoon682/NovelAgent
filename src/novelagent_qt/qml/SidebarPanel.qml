import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// SidebarPanel — 左侧栏：应用标题 + 当前项目卡片 + 会话列表 + 设置入口。
Rectangle {
    id: root
    color: Theme.bgSidebar

    // 点击设置齿轮时发射，由 MainWindow 打开设置对话框
    signal settingsRequested()

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

        // ── 会话列表（来自 bridge.sessionList()，按最近使用降序）──
        ListView {
            id: sessionList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            leftMargin: Theme.gapSm
            rightMargin: Theme.gapSm
            spacing: 2

            model: ListModel { id: sessionsModel }

            function reload() {
                sessionsModel.clear()
                if (!bridge.agentReady) return
                var list = bridge.sessionList()
                for (var i = 0; i < list.length; ++i) {
                    sessionsModel.append({ sid: list[i].id, name: list[i].title,
                                           active: list[i].active })
                }
            }
            Component.onCompleted: reload()

            delegate: Rectangle {
                width: sessionList.width - Theme.gapSm * 2
                height: 36
                radius: Theme.radiusSm
                color: model.active || rowHover.hovered ? Theme.bgHover : "transparent"

                // HoverHandler 不与 MouseArea 互斥，悬停时同时高亮行 + 显示删除按钮
                HoverHandler { id: rowHover }

                // 整行点击切换会话（删除按钮的 MouseArea 在其上层，不受影响）
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (!model.active) bridge.switchSession(model.sid)
                }

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
                    // 删除按钮：悬停行时可见；删除 active 会话后自动切到最近会话
                    Label {
                        text: "\u00d7"
                        visible: rowHover.hovered
                        font.pixelSize: 15
                        color: deleteMa.containsMouse ? Theme.warning : Theme.textFaint

                        MouseArea {
                            id: deleteMa
                            anchors.fill: parent
                            anchors.margins: -6
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: bridge.deleteSession(model.sid)
                        }
                    }
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
                ToolTip.text: "设置"
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
                    onClicked: root.settingsRequested()
                }
            }
        }
    }

    // 会话列表随后端变化刷新（新建/切换/删除/标题自动提取/Agent 重建）
    Connections {
        target: bridge
        function onSessionsChanged() { sessionList.reload() }
        function onAgentReadyChanged() { sessionList.reload() }
    }
}
