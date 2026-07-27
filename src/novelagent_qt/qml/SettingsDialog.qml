import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// SettingsDialog — 墨染设置：Provider / 项目 / 调试 三选项卡。
// 所有读写通过 bridge 的 Q_INVOKABLE 完成，保存即落盘 config.json。
Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 560
    height: 540
    modal: true
    padding: 0

    background: Rectangle {
        color: Theme.bgElevated
        radius: Theme.radiusMd
        border.width: 1
        border.color: Theme.divider
    }

    // 打开并定位到指定选项卡：0=Provider 1=项目 2=调试
    function openAt(tabIndex) {
        tabs.currentIndex = tabIndex
        open()
    }

    onOpened: providerPage.reload()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Label {
            text: "墨染设置"
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.sizeDisplay
            font.weight: Font.Bold
            color: Theme.textPrimary
            Layout.margins: Theme.gapLg
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapLg
            Layout.rightMargin: Theme.gapLg
            background: Rectangle { color: "transparent" }

            component SettingsTab: TabButton {
                contentItem: Text {
                    text: parent.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeUi
                    color: parent.checked ? Theme.accent : Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                }
                background: Rectangle {
                    color: "transparent"
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width; height: 2
                        color: parent.parent.checked ? Theme.accent : "transparent"
                    }
                }
            }
            SettingsTab { text: "模型" }
            SettingsTab { text: "项目" }
            SettingsTab { text: "调试" }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.gapLg

            // ── 选项卡 1: Provider ──
            ColumnLayout {
                id: providerPage
                spacing: Theme.gapMd

                function reload() {
                    providerCombo.model = bridge.listProviders()
                    var idx = providerCombo.model.indexOf(bridge.defaultProvider())
                    providerCombo.currentIndex = idx >= 0 ? idx : 0
                    loadFields()
                }
                function loadFields() {
                    var info = bridge.providerInfo(providerCombo.currentText)
                    apiKeyField.text = info.api_key || ""
                    modelField.text = info.model || ""
                    baseUrlField.text = info.base_url || ""
                    tempSlider.value = info.temperature !== undefined ? info.temperature : 0.7
                }
                function collect() {
                    return {
                        "api_key": apiKeyField.text.trim(),
                        "model": modelField.text.trim(),
                        "base_url": baseUrlField.text.trim(),
                        "temperature": tempSlider.value
                    }
                }

                component FieldLabel: Label {
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeCaption
                    color: Theme.textSecondary
                }

                FieldLabel { text: "Provider" }
                ComboBox {
                    id: providerCombo
                    Layout.fillWidth: true
                    onActivated: providerPage.loadFields()
                }

                FieldLabel { text: "API Key" }
                TextField {
                    id: apiKeyField
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: "sk-..."
                }

                FieldLabel { text: "模型名" }
                TextField { id: modelField; Layout.fillWidth: true }

                FieldLabel { text: "Base URL" }
                TextField { id: baseUrlField; Layout.fillWidth: true }

                RowLayout {
                    FieldLabel { text: "Temperature" }
                    Slider {
                        id: tempSlider
                        Layout.fillWidth: true
                        from: 0.0; to: 2.0; stepSize: 0.1
                    }
                    Label {
                        text: tempSlider.value.toFixed(1)
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.sizeCaption
                        color: Theme.textPrimary
                    }
                }

                Item { Layout.fillHeight: true }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: Theme.gapSm
                    Button {
                        text: "保存"
                        onClicked: bridge.saveProvider(providerCombo.currentText,
                                                       providerPage.collect())
                    }
                    Button {
                        text: "保存并启用"
                        highlighted: true
                        onClicked: {
                            bridge.saveProvider(providerCombo.currentText,
                                                providerPage.collect())
                            if (bridge.initialize(providerCombo.currentText))
                                root.close()
                        }
                    }
                }
            }

            // ── 选项卡 2: 项目 ──
            ColumnLayout {
                spacing: Theme.gapMd

                Label {
                    text: "当前项目：" + bridge.projectName
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeUi
                    color: Theme.textPrimary
                }
                Label {
                    text: bridge.projectPath === "" ? "（未打开）" : bridge.projectPath
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeCaption
                    color: Theme.textFaint
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                Button {
                    text: "打开已有项目..."
                    Layout.fillWidth: true
                    onClicked: openFolderDlg.open()
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                Label {
                    text: "新建项目"
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeUi
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                }
                TextField {
                    id: newTitleField
                    Layout.fillWidth: true
                    placeholderText: "小说名称"
                }
                RowLayout {
                    TextField {
                        id: newDirField
                        Layout.fillWidth: true
                        placeholderText: "项目目录（选择空目录或新建目录）"
                        readOnly: true
                    }
                    Button { text: "浏览..."; onClicked: newFolderDlg.open() }
                }
                Button {
                    text: "创建项目"
                    highlighted: true
                    enabled: newTitleField.text.trim().length > 0 && newDirField.text.length > 0
                    Layout.alignment: Qt.AlignRight
                    onClicked: {
                        if (bridge.createProject(newDirField.text, newTitleField.text))
                            root.close()
                    }
                }

                Item { Layout.fillHeight: true }
            }

            // ── 选项卡 3: 调试 ──
            ColumnLayout {
                spacing: Theme.gapMd
                RowLayout {
                    Label {
                        text: "启用调试日志"
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.sizeUi
                        color: Theme.textPrimary
                        Layout.fillWidth: true
                    }
                    Switch {
                        checked: bridge.verboseEnabled()
                        onToggled: bridge.setVerbose(checked)
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }

    FolderDialog {
        id: openFolderDlg
        title: "选择小说项目目录"
        onAccepted: {
            if (bridge.openProject(selectedFolder.toString()))
                root.close()
        }
    }

    FolderDialog {
        id: newFolderDlg
        title: "选择新项目目录"
        onAccepted: {
            var status = bridge.validateProjectDir(selectedFolder.toString())
            if (status === "occupied") {
                newDirField.text = ""
                newDirField.placeholderText = "该目录非空且不是小说项目，请换一个"
            } else {
                newDirField.text = selectedFolder.toString()
            }
        }
    }
}
