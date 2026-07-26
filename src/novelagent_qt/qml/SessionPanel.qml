import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    spacing: 0

    // ── 会话标题栏 ──
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.gapMd
        Layout.rightMargin: Theme.gapMd
        Layout.topMargin: Theme.gapMd
        Layout.bottomMargin: Theme.gapSm
        spacing: Theme.gapSm

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

    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

    // ── 会话列表 ──
    ListView {
        id: sessionList
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        leftMargin: Theme.gapSm
        rightMargin: Theme.gapSm
        topMargin: Theme.gapSm
        spacing: 2

        model: ListModel {
            id: sessionModel
            ListElement { name: "当前会话"; active: true }
        }

        delegate: Rectangle {
            width: sessionList.width - Theme.gapSm * 2
            height: 36
            radius: Theme.radiusSm
            color: model.active ? Theme.accentSoft
                 : sessionDelegateMa.containsMouse ? Theme.bgHover : "transparent"

            RowLayout {
                anchors { fill: parent; leftMargin: Theme.gapMd; rightMargin: Theme.gapMd }
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
                id: sessionDelegateMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
            }
        }
    }

    Label {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.gapMd
        text: "更多会话管理功能开发中"
        font.family: Theme.fontUi
        font.pixelSize: Theme.sizeCaption
        color: Theme.textFaint
        horizontalAlignment: Text.AlignHCenter
    }
}
