import QtQuick
import QtQuick.Controls

// ThemedField — 主题一致输入框：圆角内嵌底色 + 聚焦朱砂描边。
//
// 占位符自绘（manual）：本组件不依赖 TextField 默认 placeholderText 机制。
// WHY：默认机制在本项目的字体/垂直对齐组合下会出现「占位符与内容重叠、不随内容隐藏、
//      且竖直溢出控件边界」的问题（历史 bug）。改为独立 Label：
//      仅当 text 为空时显示、垂直居中、宽度=内容区宽、超长省略，且不拦截鼠标。
TextField {
    id: control

    // 占位提示文案（独立属性，避免触发基类默认占位符渲染）
    property string placeholder: ""
    property color placeholderColor: Theme.textFaint

    implicitHeight: 36
    leftPadding: 12
    rightPadding: 12
    color: Theme.textPrimary
    font.family: Theme.fontUi
    font.pixelSize: Theme.sizeUi
    selectionColor: Theme.accentSoft
    selectedTextColor: "#f5efe2"
    verticalAlignment: Text.AlignVCenter

    // 关闭基类默认占位符渲染（其存在错位/重叠/不随内容隐藏的缺陷）
    placeholderText: ""

    // 手动占位符：仅当文本为空且无输入法组合文时显示（对齐 Qt 原生占位符可见条件
    // `!length && !preeditText`）；垂直居中；宽度=内容区宽，超长省略
    Label {
        visible: control.length === 0 && control.preeditText.length === 0
        text: control.placeholder
        color: control.placeholderColor
        font: control.font
        x: control.leftPadding
        y: (control.height - implicitHeight) / 2
        width: Math.max(0, control.width - control.leftPadding - control.rightPadding)
        elide: Text.ElideRight
        enabled: false   // 不拦截鼠标，点击穿透聚焦到输入框
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.bgField
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : Theme.divider
        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }
    }
}
