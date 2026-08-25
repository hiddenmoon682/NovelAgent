pragma Singleton
import QtQuick
import QtQuick.Controls

// Toast — 全局轻提示（顶部居中、1600ms 自动消失）。
// 任意 QML 内调用 Toast.show("...")；首次调用才创建弹窗（懒加载）。
QtObject {
    id: root

    property Component toastComponent: Component {
        Popup {
            id: pop
            parent: Overlay.overlay
            // Popup 不支持 anchors.topMargin，用 x/y 显式定位（顶部居中）
            x: Math.round((Overlay.overlay.width - width) / 2)
            y: Theme.gapAmple
            modal: false
            focus: false
            closePolicy: Popup.NoAutoClose
            padding: 0

            background: Rectangle {
                color: Theme.bgElevated
                border.width: 1
                border.color: Theme.divider
                radius: Theme.radiusToast
            }

            contentItem: Label {
                text: pop.text
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeUi
                color: Theme.textSecondary
                padding: Theme.gapSm
            }

            property string text: ""
            property Timer hideTimer: Timer {
                interval: 1600
                repeat: false
                onTriggered: pop.close()
            }

            onOpened: hideTimer.restart()
        }
    }

    property var popup: null

    function show(message) {
        if (root.popup === null)
            root.popup = root.toastComponent.createObject(Overlay.overlay)
        root.popup.text = message
        root.popup.open()
        root.popup.hideTimer.restart()
    }
}
