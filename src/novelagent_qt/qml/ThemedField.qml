import QtQuick
import QtQuick.Controls

// ThemedField — 主题一致输入框：圆角内嵌底色 + 聚焦朱砂描边。
// 取代 Material 默认浮动标签样式，表单标签统一置于字段上方，避免双标签。
TextField {
    id: control

    implicitHeight: 36
    leftPadding: 12
    rightPadding: 12
    color: Theme.textPrimary
    font.family: Theme.fontUi
    font.pixelSize: Theme.sizeUi
    placeholderTextColor: Theme.textFaint
    selectionColor: Theme.accentSoft
    selectedTextColor: "#f5efe2"
    verticalAlignment: Text.AlignVCenter

    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.bgField
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : Theme.divider
        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }
    }
}
