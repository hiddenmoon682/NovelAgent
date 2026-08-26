import QtQuick
import QtQuick.Controls

// ThemedSwitch — 主题一致开关，对齐 app-mockup.html 的 .switch：
// 40×22 圆角轨（off=divider / on=accent），18px 圆点（off=text-secondary / on=#f5efe2），
// 开启时圆点右移。取代 Material 默认样式。
Switch {
    id: control

    // 显式固定为 indicator 尺寸并归零 padding：Fusion 样式下 Switch 的 implicitWidth 只按
    // 空文本 + padding 计算（约 12px），不含 indicator，导致 40px 的 indicator 溢出控件，
    // 在 RowLayout 的 fillWidth 标签把它推到行右缘时，溢出的部分就渲染到内容区之外
    // （调试页开关"飞出框外"根因）。显式声明后 indicator 恰好落在控件内，任何样式均不溢出。
    implicitWidth: 40
    implicitHeight: 22
    padding: 0
    leftPadding: 0
    rightPadding: 0

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
