import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// CreateProjectDialog — 新建项目弹窗（对齐 create-project-mockup）。
// 固定目录创建：只需填书名 + 简介；就地校验非法字符与重名。
Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 420
    modal: true
    padding: 0

    signal projectCreated(string path, string title)

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animNormal } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animFast } }

    background: Rectangle {
        color: Theme.bgElevated
        radius: Theme.radiusMd
        border.width: 1
        border.color: Theme.divider
    }

    onOpened: {
        titleField.text = ""
        descArea.text = ""
        errorLabel.visible = false
        titleField.forceActiveFocus()
    }

    function validate() {
        var name = titleField.text.trim()
        if (name.length === 0) { errorLabel.visible = false; return }
        if (/[\\\/:*?"<>|]/.test(name)) {
            errorLabel.text = "标题包含非法字符：\\ / : * ? \" < > |"
            errorLabel.visible = true
            return
        }
        var list = bridge.allProjects()
        for (var i = 0; i < list.length; ++i) {
            if (list[i].title.toLowerCase() === name.toLowerCase()) {
                errorLabel.text = "同名项目已存在：" + list[i].title
                errorLabel.visible = true
                return
            }
        }
        errorLabel.visible = false
    }

    function create() {
        var name = titleField.text.trim()
        if (name.length === 0) {
            errorLabel.text = "请输入小说名称"
            errorLabel.visible = true
            return
        }
        var st = bridge.createProjectAt(name, descArea.text.trim())
        if (st === "ok") {
            root.projectCreated(bridge.lastProjectPath(), name)
            root.close()
            Toast.show("已创建并进入项目：" + name)
            return
        }
        var msg = st === "duplicate" ? "同名项目已存在"
                : st === "invalid_chars" ? "标题包含非法字符：\\ / : * ? \" < > |"
                : "创建失败，请查看状态栏提示"
        errorLabel.text = msg
        errorLabel.visible = true
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Label {
            text: "新建项目"
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.sizeTitle
            font.weight: Font.Bold
            color: Theme.textPrimary
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapAmple
            Layout.rightMargin: Theme.gapAmple
            Layout.topMargin: Theme.gapLg
            Layout.bottomMargin: Theme.gapSm
            padding: 0
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.gapAmple
            spacing: 0

            Label {
                text: "小说名称"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textSecondary
                Layout.topMargin: Theme.gapCozy
                Layout.bottomMargin: Theme.gapTight
            }
            ThemedField {
                id: titleField
                Layout.fillWidth: true
                placeholderText: "输入小说名称…"
                onTextChanged: root.validate()
                Keys.onReturnPressed: root.create()
                Keys.onEnterPressed: root.create()
            }

            Label {
                text: "小说简介"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textSecondary
                Layout.topMargin: Theme.gapRelaxed
                Layout.bottomMargin: Theme.gapTight
            }
            TextArea {
                id: descArea
                Layout.fillWidth: true
                Layout.preferredHeight: 140
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeUi
                color: Theme.textPrimary
                placeholderText: "一句话介绍你的故事…"
                placeholderTextColor: Theme.textFaint
                wrapMode: TextEdit.Wrap
                padding: Theme.gapCozy
                background: Rectangle {
                    color: Theme.bgField
                    border.width: 1
                    border.color: descArea.activeFocus ? Theme.accent : Theme.divider
                    radius: Theme.radiusSm
                }
            }

            // 错误行：固定高度占位，显示/隐藏不抖动弹窗高度
            Label {
                id: errorLabel
                visible: false
                text: ""
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeNote
                color: Theme.danger
                Layout.topMargin: Theme.gapTight
                Layout.preferredHeight: 19
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.topMargin: Theme.gapXs
                Layout.alignment: Qt.AlignRight
                spacing: Theme.gapSm
                ThemedButton { text: "取消"; onClicked: root.close() }
                ThemedButton {
                    kind: "primary"
                    text: "创建"
                    onClicked: root.create()
                }
            }
        }
    }
}
