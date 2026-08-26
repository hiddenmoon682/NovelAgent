import QtQuick

// ThemedSwitch — 主题一致开关（纯自绘，不依赖 Qt Quick Controls 的 Switch 基类）。
//
// WHY 自绘：Qt Quick Controls 的 Switch 基类在不同样式（尤其 Fusion）下对 indicator 的
// 默认布局不一致——AbstractButton 会按 contentItem/spacing/padding 动态摆放 indicator，
// 自定义的 indicator（无显式 x/y）会被推到行右缘并溢出控件/内容区（调试页开关反复
// "飞出框外"的根因）。纯自绘 Item + MouseArea 完全自控位置与尺寸，任何样式下都稳定为
// 40×22，绝不溢出。对外仍暴露 checked / toggled(bool)，与 Switch 用法一致。
Item {
    id: control

    // 对外 API（对齐 Switch）：读取/写入选中态，点击后发 toggled(新值)
    property bool checked: false
    signal toggled(bool checked)

    implicitWidth: 40
    implicitHeight: 22
    opacity: enabled ? 1.0 : 0.4

    // 轨道：on=朱砂 / off=divider，圆角全圆
    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: control.checked ? Theme.accent : Theme.divider
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
    }

    // 圆点：on 右移（浅色）/ off 左移（次级文字色）
    Rectangle {
        x: control.checked ? parent.width - width - 2 : 2
        anchors.verticalCenter: parent.verticalCenter
        width: 18
        height: 18
        radius: 9
        color: control.checked ? "#f5efe2" : Theme.textSecondary
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
        Behavior on x { NumberAnimation { duration: Theme.animFast } }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            control.checked = !control.checked
            control.toggled(control.checked)
        }
    }
}
