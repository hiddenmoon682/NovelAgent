import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// SkillPopup — 技能管理弹窗（墨染书房风格）。
// 发送消息前用户可在此查看/启用/禁用技能，或发起「创建新技能」对话。
// 数据来源 bridge.skillList()，开关调用 bridge.setSkillEnabled()。
Popup {
    id: root

    // 「创建新技能」被点击时发射，由 AgentPanel 填入引导语并发送。
    signal createSkillRequested()

    width: 380
    height: Math.min(460, headerRow.height + skillView.contentHeight
                          + footerCol.height + Theme.gapLg * 3 + 24)
    padding: Theme.gapLg
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property var skills: []

    function reload() {
        skills = bridge.skillList()
    }

    onAboutToShow: reload()

    Connections {
        target: bridge
        function onSkillsChanged() {
            if (root.visible) root.reload()
        }
    }

    background: Rectangle {
        radius: Theme.radiusMd
        color: Theme.bgElevated
        border.width: 1
        border.color: Theme.divider
    }

    contentItem: ColumnLayout {
        spacing: Theme.gapMd

        // ── 标题 ──
        RowLayout {
            id: headerRow
            Layout.fillWidth: true

            Label {
                text: "技能"
                font.family: Theme.fontDisplay
                font.pixelSize: Theme.sizeTitle
                font.weight: Font.Bold
                color: Theme.textPrimary
            }
            Label {
                text: {
                    var n = 0
                    for (var i = 0; i < root.skills.length; ++i)
                        if (root.skills[i].enabled) n++
                    return n + " / " + root.skills.length + " 已启用"
                }
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textFaint
            }
            Item { Layout.fillWidth: true }
        }

        // ── 技能列表 ──
        ListView {
            id: skillView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.gapSm
            model: root.skills
            boundsBehavior: Flickable.StopAtBounds

            // 空状态
            Label {
                anchors.centerIn: parent
                visible: root.skills.length === 0
                text: "暂无技能\n点击下方按钮创建第一个技能"
                horizontalAlignment: Text.AlignHCenter
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeUi
                color: Theme.textFaint
            }

            delegate: Rectangle {
                width: skillView.width
                height: skillCol.height + Theme.gapMd * 2
                radius: Theme.radiusSm
                color: itemMa.containsMouse ? Theme.bgHover : "transparent"
                border.width: 1
                border.color: Theme.divider
                opacity: modelData.enabled ? 1.0 : 0.55
                Behavior on color { ColorAnimation { duration: Theme.animFast } }
                Behavior on opacity { NumberAnimation { duration: Theme.animFast } }

                MouseArea {
                    id: itemMa
                    anchors.fill: parent
                    hoverEnabled: true
                }

                RowLayout {
                    anchors {
                        left: parent.left; right: parent.right
                        leftMargin: Theme.gapMd; rightMargin: Theme.gapMd
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: Theme.gapMd

                    Column {
                        id: skillCol
                        Layout.fillWidth: true
                        spacing: 2

                        Row {
                            spacing: Theme.gapXs
                            Label {
                                text: modelData.name
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.sizeUi
                                font.weight: Font.DemiBold
                                color: Theme.textPrimary
                            }
                            Rectangle {
                                visible: modelData.always
                                anchors.verticalCenter: parent.verticalCenter
                                width: alwaysTag.width + 10
                                height: 16
                                radius: 8
                                color: "transparent"
                                border.width: 1
                                border.color: Theme.agentTint
                                Label {
                                    id: alwaysTag
                                    anchors.centerIn: parent
                                    text: "常驻"
                                    font.family: Theme.fontUi
                                    font.pixelSize: 9
                                    color: Theme.agentTint
                                }
                            }
                        }
                        Label {
                            width: skillCol.width
                            text: modelData.description
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.sizeCaption
                            color: Theme.textSecondary
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }

                    // 启用开关
                    Switch {
                        id: sw
                        checked: modelData.enabled
                        onToggled: {
                            if (!bridge.setSkillEnabled(modelData.name, checked))
                                checked = modelData.enabled  // 失败回滚
                        }

                        indicator: Rectangle {
                            implicitWidth: 36
                            implicitHeight: 20
                            radius: 10
                            color: sw.checked ? Theme.accent : Theme.bgHover
                            border.width: 1
                            border.color: sw.checked ? Theme.accent : Theme.divider
                            Behavior on color { ColorAnimation { duration: Theme.animFast } }

                            Rectangle {
                                x: sw.checked ? parent.width - width - 3 : 3
                                anchors.verticalCenter: parent.verticalCenter
                                width: 14; height: 14; radius: 7
                                color: "#f5efe2"
                                Behavior on x { NumberAnimation { duration: Theme.animFast } }
                            }
                        }
                    }
                }
            }
        }

        // ── 底部：创建新技能 ──
        Column {
            id: footerCol
            Layout.fillWidth: true
            spacing: Theme.gapSm

            Rectangle { width: parent.width; height: 1; color: Theme.divider }

            Rectangle {
                width: parent.width
                height: 40
                radius: Theme.radiusSm
                color: createMa.containsMouse ? Theme.bgHover : "transparent"
                border.width: 1
                border.color: createMa.containsMouse ? Theme.accent : Theme.divider
                Behavior on color { ColorAnimation { duration: Theme.animFast } }
                Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

                Label {
                    anchors.centerIn: parent
                    text: "＋ 创建新技能"
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeUi
                    color: createMa.containsMouse ? Theme.accent : Theme.textSecondary
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }
                MouseArea {
                    id: createMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    enabled: !bridge.busy
                    onClicked: {
                        root.createSkillRequested()
                        root.close()
                    }
                }
            }
        }
    }
}
