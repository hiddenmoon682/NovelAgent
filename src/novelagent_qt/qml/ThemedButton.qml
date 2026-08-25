import QtQuick
import QtQuick.Controls

// ThemedButton — 主题一致按钮：扁平圆角、无 Material 抬升阴影。
// kind: "normal" 描边次级按钮 | "primary" 朱砂实心 | "danger" 危险实心 | "text" 无边框文本按钮
Button {
    id: control

    property string kind: "normal"

    readonly property bool solid: kind === "primary" || kind === "danger"

    implicitWidth: Math.max(64, contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: 32
    leftPadding: 14
    rightPadding: 14

    contentItem: Text {
        text: control.text
        font.family: Theme.fontUi
        font.pixelSize: Theme.sizeUi
        color: !control.enabled ? Theme.textFaint
             : control.solid ? "#f5efe2"
             : (control.hovered || control.pressed) ? Theme.textPrimary : Theme.textSecondary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: Theme.radiusSm
        border.width: control.kind === "normal" && control.enabled ? 1 : 0
        border.color: Theme.divider
        color: !control.enabled ? "transparent"
             : control.kind === "primary" ? (control.hovered ? Qt.lighter(Theme.accent, 1.12) : Theme.accent)
             : control.kind === "danger" ? (control.hovered ? Qt.lighter(Theme.danger, 1.12) : Theme.danger)
             : (control.hovered || control.pressed) ? Theme.bgHover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
    }
}
