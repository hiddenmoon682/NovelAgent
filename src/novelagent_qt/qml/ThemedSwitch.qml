import QtQuick
import QtQuick.Controls

// ThemedSwitch — 主题一致开关，对齐 app-mockup.html 的 .switch：
// 40×22 圆角轨（off=divider / on=accent），18px 圆点（off=text-secondary / on=#f5efe2），
// 开启时圆点右移。取代 Material 默认样式。
Switch {
    id: control

    indicator: Rectangle {
        implicitWidth: 40
        implicitHeight: 22
        radius: 11
        color: control.checked ? Theme.accent : Theme.divider
        Behavior on color { ColorAnimation { duration: Theme.animFast } }

        Rectangle {
            x: control.checked ? parent.width - width - 2 : 2
            anchors.verticalCenter: parent.verticalCenter
            width: 18; height: 18; radius: 9
            color: control.checked ? "#f5efe2" : Theme.textSecondary
            Behavior on color { ColorAnimation { duration: Theme.animFast } }
            Behavior on x { NumberAnimation { duration: Theme.animFast } }
        }
    }
}
