import QtQuick

// ModalDimmer — 统一模态遮罩（深色半透明）。
// Qt 6 起 Overlay.modal / Overlay.modeless 只能挂在 Popup 上（官方文档：
// "The property can be attached to any popup"）；挂在 ApplicationWindow 或普通
// Item 上静默无效——历史踩坑：遮罩配置写在 MainWindow 上从未生效，弹窗四周
// 仍是 Material 默认偏浅（白色）遮罩。因此抽成共享组件，由各模态弹窗引用：
//     Overlay.modal: ModalDimmer {}
// anchors.fill 铺满所属 Overlay（窗口），避免 0×0 不渲染。
Rectangle {
    anchors.fill: parent
    color: Theme.overlayDim
}
