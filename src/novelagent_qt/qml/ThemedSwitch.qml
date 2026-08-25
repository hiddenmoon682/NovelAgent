import QtQuick
import QtQuick.Controls

// ThemedSwitch — 主题一致开关：朱砂开启 / 墨灰关闭，取代 Material 默认样式。
Switch {
    id: control

    indicator: Rectangle {
        implicitWidth: 36
        implicitHeight: 20
        radius: 10
        color: control.checked ? Theme.accent : Theme.bgHover
        border.width: 1
        border.color: control.checked ? Theme.accent : Theme.divider
        Behavior on color { ColorAnimation { duration: Theme.animFast } }

        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: 14; height: 14; radius: 7
            color: "#f5efe2"
            Behavior on x { NumberAnimation { duration: Theme.animFast } }
        }
    }
}
