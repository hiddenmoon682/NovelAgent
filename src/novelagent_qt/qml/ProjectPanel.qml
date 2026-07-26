import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    spacing: 0

    // ── 应用标题 ──
    Rectangle {
        Layout.fillWidth: true
        height: 48
        color: "transparent"

        Label {
            anchors {
                left: parent.left
                leftMargin: Theme.gapLg
                verticalCenter: parent.verticalCenter
            }
            text: "墨染"
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.sizeTitle
            font.weight: Font.Bold
            color: Theme.textPrimary
        }
    }

    // ── 新建项目按钮 ──
    Rectangle {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.gapSm
        Layout.rightMargin: Theme.gapSm
        Layout.bottomMargin: Theme.gapSm
        height: 36
        radius: Theme.radiusSm
        color: newProjectMa.containsMouse ? Theme.bgHover : "transparent"

        RowLayout {
            anchors { fill: parent; leftMargin: Theme.gapMd; rightMargin: Theme.gapMd }
            Label {
                text: "+"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeBody
                color: Theme.accent
            }
            Label {
                text: "新建项目"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeUi
                color: Theme.textPrimary
                Layout.fillWidth: true
            }
        }

        MouseArea {
            id: newProjectMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: { /* TODO: bridge.newProject() */ }
        }
    }

    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

    // ── 项目列表 ──
    Label {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.gapLg
        Layout.topMargin: Theme.gapMd
        Layout.bottomMargin: Theme.gapSm
        text: "项目"
        font.family: Theme.fontUi
        font.pixelSize: Theme.sizeCaption
        font.weight: Font.DemiBold
        color: Theme.textSecondary
    }

    ListView {
        id: projectList
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        leftMargin: Theme.gapSm
        rightMargin: Theme.gapSm
        spacing: 2

        model: ListModel {
            id: projectModel
            ListElement { name: "未打开项目"; active: true }
        }

        delegate: Rectangle {
            width: projectList.width - Theme.gapSm * 2
            height: 36
            radius: Theme.radiusSm
            color: model.active ? Theme.accentSoft
                 : projectDelegateMa.containsMouse ? Theme.bgHover : "transparent"

            RowLayout {
                anchors { fill: parent; leftMargin: Theme.gapMd; rightMargin: Theme.gapMd }
                Label {
                    text: "\uD83D\uDCC1"
                    font.pixelSize: 14
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
                id: projectDelegateMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
            }
        }
    }

    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

    // ── 底部齿轮（设置入口）──
    Rectangle {
        Layout.fillWidth: true
        height: 44
        color: "transparent"

        RowLayout {
            anchors { fill: parent; leftMargin: Theme.gapMd; rightMargin: Theme.gapMd }

            Item { Layout.fillWidth: true }

            Rectangle {
                width: 32
                height: 32
                radius: Theme.radiusSm
                color: settingsMa.containsMouse ? Theme.bgHover : "transparent"

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
                    onClicked: { /* TODO: open settings dialog */ }
                }
            }
        }
    }
}
