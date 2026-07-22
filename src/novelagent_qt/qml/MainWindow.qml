import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// MainWindow — 主窗口三栏布局。
// 左栏：会话管理 + 设置 | 中栏：Agent 对话 | 右栏：文本展示
ApplicationWindow {
    id: window
    width: 1400
    height: 900
    minimumWidth: 1000
    minimumHeight: 600
    visible: true
    title: "墨染 · AI小说创作助手"

    // 深色背景
    color: Theme.bgDeep

    // Material 主题暗色模式
    Material.theme: Material.Dark
    Material.accent: Theme.accent

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        // ── 左栏：会话管理 + 设置 ──
        SessionPanel {
            SplitView.preferredWidth: 260
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: 360
        }

        // ── 中栏：Agent 对话 ──
        AgentPanel {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 400
        }

        // ── 右栏：文本展示 ──
        ReaderPanel {
            id: readerPanel
            SplitView.preferredWidth: 420
            SplitView.minimumWidth: 300
        }
    }

    // ── 底部状态栏 ──
    footer: StatusBar {}

    // ── 全局快捷键 ──
    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: window.close()
    }
}
