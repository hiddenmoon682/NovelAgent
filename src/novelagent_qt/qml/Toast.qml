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
            // parent 由 show() 的 createObject(overlay) 显式传入。
            // 注意：不能在此写 parent: Overlay.overlay —— 单例 QtObject 上下文里
            // attached property 求值为 null（历史 QML 警告：Toast.qml:15 读取 null.width）。
            property string text: ""
            property Timer hideTimer: Timer {
                interval: 1600
                repeat: false
                onTriggered: pop.close()
            }

            // Popup 不支持 anchors.topMargin，用 x/y 显式定位（顶部居中）；
            // parent 即 overlay（createObject 传入），防御性判空避免单例上下文时序
            x: pop.parent ? Math.round((pop.parent.width - width) / 2) : 0
            y: pop.parent ? Theme.gapAmple : 0
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

            onOpened: hideTimer.restart()
        }
    }

    property var popup: null

    function show(message) {
        // overlay 对象必须在有窗口上下文的调用点取（如 MainWindow 内），
        // 不能依赖单例自身上下文求值（会得 null）
        var overlay = Overlay.overlay
        if (!overlay) return
        if (root.popup === null)
            root.popup = root.toastComponent.createObject(overlay)
        root.popup.text = message
        root.popup.open()
        root.popup.hideTimer.restart()
    }
}
