import QtQuick
import QtQuick.Controls

// ThemedCombo — 主题一致下拉框：圆角内嵌底色 + 单色 ▾ 指示器 + 主题化弹出列表。
ComboBox {
    id: control

    implicitHeight: 36
    leftPadding: 12
    rightPadding: 32
    font.family: Theme.fontUi
    font.pixelSize: Theme.sizeUi

    contentItem: Text {
        text: control.displayText
        font: control.font
        color: Theme.textPrimary
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    indicator: Text {
        x: control.width - width - control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        text: "\u25be"
        font.pixelSize: Theme.sizeUi
        color: Theme.textSecondary
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.bgField
        border.width: 1
        border.color: control.pressed || control.hovered ? Theme.textFaint : Theme.divider
        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(listView.contentHeight + padding * 2, 260)
        padding: 4

        background: Rectangle {
            radius: Theme.radiusSm
            color: Theme.bgElevated
            border.width: 1
            border.color: Theme.divider
        }

        contentItem: ListView {
            id: listView
            clip: true
            model: control.popup.visible ? control.visualModel : null
            ScrollBar.vertical: ScrollBar {}
        }
    }

    delegate: Rectangle {
        width: listView.width
        height: 32
        radius: Theme.radiusSm
        color: itemMa.containsMouse || control.currentIndex === index
               ? Theme.bgHover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.animFast } }

        Label {
            anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
            text: control.textAt(index)
            font.family: Theme.fontUi
            font.pixelSize: Theme.sizeUi
            color: control.currentIndex === index ? Theme.accent : Theme.textPrimary
            elide: Text.ElideRight
        }

        MouseArea {
            id: itemMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                control.currentIndex = index
                control.popup.close()
            }
        }
    }
}
