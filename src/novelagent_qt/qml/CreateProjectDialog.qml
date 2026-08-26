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

    // 模态遮罩：Qt 6 中 Overlay.modal 只能挂在 Popup 上，统一引用共享遮罩组件
    Overlay.modal: ModalDimmer {}

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
            Layout.topMargin: 18
            Layout.bottomMargin: Theme.gapSm
            padding: 0
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 0
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
                // 关闭基类默认占位符，改用手动占位 Label（对齐 Qt 原生 PlaceholderText 布局与可见条件）
                placeholderText: ""
                wrapMode: TextEdit.Wrap
                // 用显式 per-side padding 覆盖 Material 动态 topPadding：Material 样式下
                // TextArea.topPadding 会因自定义 background 无 implicitHeight 而被算成负值，
                // 使内容与输入光标（content 起点）渲染到框外。此处显式写死四边 padding，
                // 让内容/光标从框内 (12,10) 起渲染，与占位符对齐。
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
                    // 手动占位符：作为可见框 background 的子项、锚定左上。
                    // 不依赖 descArea.topPadding —— Material 样式下 TextArea.topPadding 会因自定义
                    // background 无 implicitHeight 而被算成负值，占位符按该 y 渲染会「飞出框外」，
                    // 锚定到可见框即保证始终在框内。可见条件同原生（无正文且无输入法组合文），
                    // 组合态（拼音/五笔上屏中）一输入即隐藏，避免与正文重叠。置于内容区左上，不拦截鼠标。
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

            // 错误行：零占位（复刻原型 height:0 + overflow 溢出绘制），
            // 显示/隐藏均不影响按钮行位置；文字向下溢出 12px 字号自身高度
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
                    text: "创建"
                    onClicked: root.create()
                }
            }
        }
    }
}
