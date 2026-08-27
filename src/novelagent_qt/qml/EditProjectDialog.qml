import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// EditProjectDialog — 编辑项目弹窗（仿照 CreateProjectDialog）：修改项目名称与简介。
// 预填由 openFor() 传入（调用方从 allProjects 取 title/description）；
// 校验规则与新建一致（非法字符、重名——排除被编辑项目自身）。
Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 420
    modal: true
    padding: 0

    // 模态遮罩：Qt 6 中 Overlay.modal 只能挂在 Popup 上，统一引用共享遮罩组件
    Overlay.modal: ModalDimmer {}

    signal projectEdited()

    property string projectPath: ""
    property string projectTitle: ""
    property string projectDescription: ""

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animNormal } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animFast } }

    background: Rectangle {
        color: Theme.bgElevated
        radius: Theme.radiusMd
        border.width: 1
        border.color: Theme.divider
    }

    // 打开并按待编辑项目的当前信息预填（调用方传入全部字段，避免再次查询的时序问题）
    function openFor(path, title, description) {
        root.projectPath = path
        root.projectTitle = title
        root.projectDescription = description
        titleField.text = title
        descArea.text = description
        errorLabel.visible = false
        root.open()
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
            // 排除被编辑项目自身：名称未变不报重名
            if (list[i].path === root.projectPath) continue
            if (list[i].title.toLowerCase() === name.toLowerCase()) {
                errorLabel.text = "同名项目已存在：" + list[i].title
                errorLabel.visible = true
                return
            }
        }
        errorLabel.visible = false
    }

    function save() {
        var name = titleField.text.trim()
        if (name.length === 0) {
            errorLabel.text = "请输入小说名称"
            errorLabel.visible = true
            return
        }
        var st = bridge.editProject(root.projectPath, name, descArea.text.trim())
        if (st === "ok") {
            root.projectEdited()
            root.close()
            Toast.show("已保存项目信息")
            return
        }
        var msg = st === "duplicate" ? "同名项目已存在"
                : st === "invalid_chars" ? "标题包含非法字符：\\ / : * ? \" < > |"
                : st === "not_found" ? "项目不存在或已被删除"
                : "保存失败，请查看状态栏提示"
        errorLabel.text = msg
        errorLabel.visible = true
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Label {
            text: "编辑项目"
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.sizeTitle
            font.weight: Font.Bold
            color: Theme.textPrimary
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapAmple
            Layout.rightMargin: Theme.gapAmple
            Layout.topMargin: 18
            Layout.bottomMargin: Theme.gapSm
            padding: 0
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapAmple
            Layout.rightMargin: Theme.gapAmple
            Layout.bottomMargin: 12
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
                placeholder: "输入小说名称…"
                onTextChanged: root.validate()
                Keys.onReturnPressed: root.save()
                Keys.onEnterPressed: root.save()
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
                // 关闭基类默认占位符，改用手动占位 Label（对齐 CreateProjectDialog 的做法）
                placeholderText: ""
                wrapMode: TextEdit.Wrap
                leftPadding: Theme.gapMd
                topPadding: Theme.gapCozy
                rightPadding: Theme.gapMd
                bottomPadding: Theme.gapCozy
                background: Rectangle {
                    id: descBg
                    color: Theme.bgField
                    border.width: 1
                    border.color: descArea.activeFocus ? Theme.accent : Theme.divider
                    radius: Theme.radiusSm
                    clip: true
                    // 手动占位符：锚定到可见框左上（Material 样式下 TextArea.topPadding 不可靠，
                    // 与 CreateProjectDialog 同源处理），可见条件同原生（无正文且无输入法组合文）
                    Label {
                        visible: descArea.length === 0 && descArea.preeditText.length === 0
                        text: "一句话介绍你的故事…"
                        color: Theme.textFaint
                        font: descArea.font
                        anchors { left: parent.left; top: parent.top; leftMargin: Theme.gapMd; topMargin: Theme.gapCozy }
                        width: parent.width - (Theme.gapMd + Theme.gapCozy)
                        elide: Text.ElideRight
                        enabled: false
                    }
                }
            }

            // 错误行：零占位（复刻新建弹窗做法），显示/隐藏均不影响按钮行位置
            Label {
                id: errorLabel
                visible: false
                text: ""
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeNote
                color: Theme.danger
                Layout.preferredHeight: 0
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.topMargin: 18
                Layout.alignment: Qt.AlignRight
                spacing: Theme.gapSm
                ThemedButton { text: "取消"; onClicked: root.close() }
                ThemedButton {
                    kind: "primary"
                    text: "保存"
                    onClicked: root.save()
                }
            }
        }
    }
}
