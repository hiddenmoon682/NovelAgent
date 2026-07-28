import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// WelcomeWizard — 首次启动向导：欢迎 → 配置模型 → 选择项目。
// tryAutoStart() 失败（无有效默认 Provider）时由 MainWindow 打开。
Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 520
    height: 480
    modal: true
    closePolicy: Popup.NoAutoClose
    padding: Theme.gapLg

    background: Rectangle {
        color: Theme.bgElevated
        radius: Theme.radiusMd
        border.width: 1
        border.color: Theme.divider
    }

    onOpened: {
        pages.currentIndex = 0
        providerCombo.model = bridge.listProviders()
        var idx = providerCombo.model.indexOf(bridge.defaultProvider())
        providerCombo.currentIndex = idx >= 0 ? idx : 0
        loadProviderFields()
    }

    function loadProviderFields() {
        var info = bridge.providerInfo(providerCombo.currentText)
        keyField.text = info.hasKey ? info.api_key : ""
        modelField.text = info.model || ""
    }

    // 保存 provider 并初始化 Agent；成功返回 true
    function applyProvider() {
        bridge.saveProvider(providerCombo.currentText, {
            "api_key": keyField.text.trim(),
            "model": modelField.text.trim()
        })
        return bridge.initialize(providerCombo.currentText)
    }

    StackLayout {
        id: pages
        anchors.fill: parent

        // ── 第 0 页：欢迎 ──
        ColumnLayout {
            spacing: Theme.gapMd
            Item { Layout.fillHeight: true }
            Label {
                text: "墨染"
                font.family: Theme.fontDisplay
                font.pixelSize: 42
                font.weight: Font.Bold
                color: Theme.textPrimary
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: "AI 小说创作助手\n首次使用需要完成两步配置：模型与项目"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeUi
                color: Theme.textSecondary
                horizontalAlignment: Text.AlignHCenter
                Layout.alignment: Qt.AlignHCenter
            }
            Item { Layout.fillHeight: true }
            Button {
                text: "开始配置"
                highlighted: true
                Layout.alignment: Qt.AlignHCenter
                onClicked: pages.currentIndex = 1
            }
        }

        // ── 第 1 页：模型配置 ──
        ColumnLayout {
            spacing: Theme.gapMd
            Label {
                text: "第 1 步 / 共 2 步：配置模型"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textFaint
            }
            Label {
                text: "选择 Provider"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textSecondary
            }
            ComboBox {
                id: providerCombo
                Layout.fillWidth: true
                onActivated: root.loadProviderFields()
            }
            Label {
                text: "API Key"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textSecondary
            }
            TextField {
                id: keyField
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: "sk-..."
            }
            Label {
                text: "模型名"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textSecondary
            }
            TextField { id: modelField; Layout.fillWidth: true }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { text: "上一步"; onClicked: pages.currentIndex = 0 }
                Button {
                    text: "下一步"
                    highlighted: true
                    enabled: keyField.text.trim().length > 0
                    onClicked: {
                        if (root.applyProvider())
                            pages.currentIndex = 2
                        // 失败时 bridge 会 emit errorOccurred，由聊天面板错误提示展示
                    }
                }
            }
        }

        // ── 第 2 页：项目 ──
        ColumnLayout {
            spacing: Theme.gapMd
            Label {
                text: "第 2 步 / 共 2 步：选择项目"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textFaint
            }
            Button {
                text: "打开已有项目..."
                Layout.fillWidth: true
                onClicked: wizardOpenDlg.open()
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
            Label {
                text: "或新建项目"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textSecondary
            }
            TextField {
                id: wizardTitleField
                Layout.fillWidth: true
                placeholderText: "小说名称"
            }
            RowLayout {
                TextField {
                    id: wizardDirField
                    Layout.fillWidth: true
                    readOnly: true
                    placeholderText: "项目目录"
                }
                Button { text: "浏览..."; onClicked: wizardNewDlg.open() }
            }
            Button {
                text: "创建并进入"
                highlighted: true
                Layout.fillWidth: true
                enabled: wizardTitleField.text.trim().length > 0 && wizardDirField.text.length > 0
                onClicked: {
                    if (bridge.createProject(wizardDirField.text, wizardTitleField.text))
                        root.close()
                }
            }
            Item { Layout.fillHeight: true }
            Button {
                text: "暂时跳过（稍后可在设置中打开项目）"
                flat: true
                Layout.alignment: Qt.AlignHCenter
                onClicked: root.close()   // Agent 已在第 1 步初始化，无项目状态可用
            }
        }
    }

    FolderDialog {
        id: wizardOpenDlg
        title: "选择小说项目目录"
        onAccepted: {
            if (bridge.openProject(selectedFolder.toString()))
                root.close()
        }
    }

    FolderDialog {
        id: wizardNewDlg
        title: "选择新项目目录"
        onAccepted: {
            var status = bridge.validateProjectDir(selectedFolder.toString())
            if (status !== "occupied")
                wizardDirField.text = selectedFolder.toString()
        }
    }
}
