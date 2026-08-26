import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ConfirmDialog — 通用确认弹窗（对齐 settings-mockup 删除确认）。
// 用法：设置 titleText/messageText/detailName/confirmText 后 open()，
// 用户点确认时发 confirmed()。
Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 380
    modal: true
    padding: 0

    // 模态遮罩：Qt 6 中 Overlay.modal 只能挂在 Popup 上，统一引用共享遮罩组件
    Overlay.modal: ModalDimmer {}

    property string titleText: "确认"
    property string messageText: ""
    property string detailName: ""     // 正文中朱砂高亮显示的名字
    property string confirmText: "删除"
    property bool dangerConfirm: true

    signal confirmed()

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animNormal } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animFast } }

    background: Rectangle {
        color: Theme.bgElevated
        radius: Theme.radiusMd
        border.width: 1
        border.color: Theme.divider
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Label {
            text: root.titleText
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.sizeTitle
            font.weight: Font.Bold
            color: Theme.textPrimary
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapAmple
            Layout.rightMargin: Theme.gapAmple
            Layout.topMargin: Theme.gapLg
            Layout.bottomMargin: Theme.gapSm
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

        Label {
            id: messageLabel
            // 绑定计算渲染，避免事件时序问题（%1 替换不再依赖 onTextChanged：
            // 事件在 messageText 先于 detailName 设置时不会触发二次替换）
            textFormat: Text.RichText
            // 仅当 detailName 非空时把 %1 替换为朱砂色高亮名字
            text: root.detailName !== ""
                  ? root.messageText.replace("%1",
                      '<span style="color:' + Theme.accent + '">'
                      + messageLabel.escapeHtml(root.detailName) + "</span>")
                  : root.messageText
            font.family: Theme.fontUi
            font.pixelSize: Theme.sizeUi
            color: Theme.textPrimary
            lineHeight: 1.8
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapAmple
            Layout.rightMargin: Theme.gapAmple
            Layout.topMargin: Theme.gapLg

            // 项目名可能含 < > & 等字符（Windows 目录名限制已排除 \ / : * ? " < > |，
            // 但防御性转义，避免 RichText 注入）
            function escapeHtml(input) {
                return input.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
            }
        }

        RowLayout {
            Layout.topMargin: Theme.gapAmple
            Layout.leftMargin: Theme.gapAmple
            Layout.rightMargin: Theme.gapAmple
            Layout.bottomMargin: 12
            Layout.alignment: Qt.AlignRight
            spacing: Theme.gapSm
            ThemedButton { text: "取消"; onClicked: root.close() }
            ThemedButton {
                kind: root.dangerConfirm ? "danger" : "primary"
                text: root.confirmText
                onClicked: { root.confirmed(); root.close() }
            }
        }
    }
}
