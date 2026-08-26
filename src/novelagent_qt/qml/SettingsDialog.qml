import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// SettingsDialog — 墨染设置（左右式，对齐 settings-mockup 原型）。
// 左 rail：模型 / 项目 / 调试；右内容区随选中切换。
// 所有读写通过 bridge 的 Q_INVOKABLE 完成，保存即落盘 config.json。
Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 780
    height: 520
    modal: true
    padding: 0

    // 模态遮罩：Qt 6 中 Overlay.modal 只能挂在 Popup 上，统一引用共享遮罩组件
    Overlay.modal: ModalDimmer {}

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animNormal } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animFast } }

    background: Rectangle {
        color: Theme.bgElevated
        radius: Theme.radiusMd
        border.width: 1
        border.color: Theme.divider
    }

    // 打开并定位到指定页：0=模型 1=项目 2=调试
    function openAt(pageIndex) {
        railRepeater.model = null   // 强制刷新选中态
        railRepeater.model = ["模型", "项目", "调试"]
        railRow.currentIndex = pageIndex
        open()
    }

    onOpened: {
        modelsPage.reload()
        projectsPage.reload()
    }

    // ── 左右主体：左 rail + 右内容（对齐原型：无标题区，rail 通顶；Esc 关闭）──
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // 左 rail（分类导航）
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 168
            color: Theme.bgSidebar
            border.width: 0
            Rectangle {
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                width: 1
                color: Theme.divider
            }

            // 左 rail 导航：竖直堆叠「模型/项目/调试」。用 Row + 整行宽 Item 会横排溢出，
            // 只有第一项可见、第二项"项"字戳进右侧内容区（历史重叠 bug 根因），故改为 Column。
            Column {
                id: railRow
                property int currentIndex: 0
                anchors { top: parent.top; topMargin: Theme.gapRelaxed; left: parent.left; right: parent.right; leftMargin: Theme.gapSm; rightMargin: Theme.gapSm }
                spacing: 0

                Repeater {
                    id: railRepeater
                    model: ["模型", "项目", "调试"]
                    delegate: Item {
                        width: railRow.width
                        height: 36

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusSm
                            color: railRow.currentIndex === index ? Theme.bgElevated : "transparent"
                        }
                        // 左侧朱砂标条
                        Rectangle {
                            visible: railRow.currentIndex === index
                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; topMargin: 7; bottomMargin: 7 }
                            width: Theme.markBar
                            radius: Theme.markBar / 2
                            color: Theme.accent
                        }
                        Label {
                            anchors { left: parent.left; leftMargin: Theme.gapMd; verticalCenter: parent.verticalCenter }
                            text: modelData
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.sizeUi
                            color: railRow.currentIndex === index ? Theme.textPrimary : Theme.textSecondary
                        }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: railRow.currentIndex = index
                        }
                    }
                }
            }
        }

        // 右内容区
        Rectangle {
            Layout.fillHeight: true
            Layout.fillWidth: true
            color: "transparent"

            StackLayout {
                anchors.fill: parent
                anchors.margins: 0
                currentIndex: railRow.currentIndex

                // ── 页 0: 模型（多模型管理）──
                Rectangle {
                    id: modelsPage
                    color: "transparent"

                    function reload() {
                        modelListModel.clear()
                        var names = bridge.listProviders()
                        var def = bridge.defaultProvider()
                        var selected = selectedProvider
                        // 首次加载：优先选中默认 provider，其次保持原选中，最后第一个
                        if (selected === "")
                            selected = def
                        for (var i = 0; i < names.length; ++i) {
                            var info = bridge.providerInfo(names[i])
                            // key = Provider 标识（map 键，稳定）；title = 显示名（命名）
                            // 注意：QML 中 id 是保留关键字，角色名用 key
                            modelListModel.append({ key: names[i],
                                                    title: info.name,
                                                    isDefault: info.isDefault,
                                                    hasKey: info.hasKey })
                            if (selected === names[i])
                                selected = names[i]
                        }
                        if (selected === "" && names.length > 0)
                            selected = names[0]
                        selectedProvider = selected
                        loadFields()
                    }

                    // 选中模型变化时回填表单
                    property string selectedProvider: ""
                    function loadFields() {
                        var info = bridge.providerInfo(selectedProvider)
                        nameField.text = info.name || ""
                        // Provider 标识：稳定只读，展示 selectedProvider（map 键）
                        providerField.text = selectedProvider
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

                    function save() {
                        if (selectedProvider === "") return
                        var vals = collect()
                        // 命名框内容变化 → 仅改显示名（Provider 标识 selectedProvider 保持稳定）
                        var newName = nameField.text.trim()
                        if (newName !== selectedProvider && newName !== "")
                            vals["rename_to"] = newName
                        if (bridge.saveProvider(selectedProvider, vals)) {
                            reload()
                            Toast.show("已保存模型「" + (newName !== "" ? newName : selectedProvider) + "」")
                        } else {
                            Toast.show("保存失败")
                        }
                    }

                    function setDefault() {
                        if (selectedProvider === "") return
                        save()
                        var label = nameField.text.trim() !== "" ? nameField.text.trim() : selectedProvider
                        if (bridge.initialize(selectedProvider)) {
                            Toast.show("已将「" + label + "」设为默认模型")
                        } else {
                            Toast.show("启用失败：请检查 API Key 是否有效")
                        }
                    }

                    RowLayout {
                        anchors { fill: parent; topMargin: Theme.gapSpacious; bottomMargin: Theme.gapSpacious; leftMargin: Theme.gapAmple; rightMargin: Theme.gapAmple }
                        spacing: Theme.gapRelaxed

                        // ── 左：模型列表 ──
                        ColumnLayout {
                            Layout.preferredWidth: 168
                            Layout.fillHeight: true
                            spacing: 0

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                color: Theme.bgField
                                radius: Theme.radiusSm
                                border.width: 1
                                border.color: Theme.divider
                                // 表头
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.gapCozy
                                    anchors.rightMargin: Theme.gapCozy
                                    Label {
                                        text: "模型列表"
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.sizeCaption
                                        color: Theme.textFaint
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: "＋"
                                        font.pixelSize: 14
                                        color: Theme.accent
                                        MouseArea {
                                            anchors.fill: parent
                                            anchors.margins: -4
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                var n = bridge.addProvider()
                                                if (n !== "") {
                                                    selectedProvider = n
                                                    modelsPage.reload()
                                                    nameField.forceActiveFocus()
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: Theme.bgField
                                radius: Theme.radiusSm
                                border.width: 1
                                border.color: Theme.divider
                                clip: true

                                ListView {
                                    id: modelList
                                    anchors.fill: parent
                                    anchors.margins: 2
                                    model: ListModel { id: modelListModel }
                                    ScrollBar.vertical: ScrollBar { width: 4; policy: ScrollBar.AsNeeded; background: Rectangle { color: "transparent" } }
                                    clip: true

                                    delegate: Rectangle {
                                        required property string key
                                        required property string title
                                        required property bool isDefault
                                        required property bool hasKey
                                        width: modelList.width
                                        height: 36
                                        radius: Theme.radiusSm
                                        color: modelRowHover.hovered ? Theme.bgHover
                                             : (key === modelsPage.selectedProvider ? Theme.accentTint : "transparent")

                                        // 选中标条
                                        Rectangle {
                                            visible: key === modelsPage.selectedProvider
                                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; topMargin: 7; bottomMargin: 7 }
                                            width: Theme.markBar
                                            radius: Theme.markBar / 2
                                            color: Theme.accent
                                        }

                                        HoverHandler { id: modelRowHover }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                modelsPage.selectedProvider = key
                                                modelsPage.loadFields()
                                            }
                                        }

                                        RowLayout {
                                            anchors { left: parent.left; leftMargin: Theme.gapMd; right: parent.right; rightMargin: Theme.gapCozy; verticalCenter: parent.verticalCenter }
                                            spacing: Theme.gapTight
                                            Label {
                                                text: title
                                                font.family: Theme.fontUi
                                                font.pixelSize: Theme.sizeUi
                                                color: key === modelsPage.selectedProvider ? Theme.textPrimary : Theme.textSecondary
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                            // "默认"徽标：Rectangle 描边 + Label
                                            Rectangle {
                                                visible: isDefault
                                                implicitWidth: defaultBadgeTxt.implicitWidth + 8
                                                implicitHeight: defaultBadgeTxt.implicitHeight + 2
                                                radius: Theme.radiusXs
                                                border.width: 1
                                                border.color: Theme.accent
                                                color: "transparent"
                                                Label {
                                                    id: defaultBadgeTxt
                                                    anchors.centerIn: parent
                                                    text: "默认"
                                                    font.family: Theme.fontUi
                                                    font.pixelSize: Theme.sizeMini
                                                    color: Theme.accent
                                                }
                                            }
                                            Label {
                                                visible: modelRowHover.hovered && !isDefault
                                                text: "✕"
                                                font.pixelSize: 12
                                                color: delHover.containsMouse ? Theme.accent : Theme.textFaint
                                                MouseArea {
                                                    id: delHover
                                                    anchors.fill: parent
                                                    anchors.margins: -4
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: {
                                                        if (bridge.deleteProvider(key)) {
                                                            modelsPage.reload()
                                                            Toast.show("已删除模型「" + title + "」")
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                // 空态（原型：暂无模型，点击右上角 ＋ 添加。）
                                Label {
                                    anchors.centerIn: parent
                                    visible: modelListModel.count === 0
                                    text: "暂无模型，点击右上角 ＋ 添加。"
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.sizeNote
                                    color: Theme.textFaint
                                }
                            }
                        }

                        // ── 右：模型表单 ──
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 0

                            component FieldLabel: Label {
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.sizeCaption
                                color: Theme.textSecondary
                                Layout.topMargin: Theme.gapMd
                                Layout.bottomMargin: Theme.gapTight
                            }

                            FieldLabel { text: "命名"; Layout.topMargin: 0 }
                            ThemedField {
                                id: nameField
                                Layout.fillWidth: true
                            }

                            // Provider：稳定标识（map 键），只读展示，与可改的显示名（命名）分离
                            FieldLabel { text: "Provider" }
                            ThemedField {
                                id: providerField
                                Layout.fillWidth: true
                                readOnly: true
                            }

                            FieldLabel { text: "API Key" }
                            ThemedField {
                                id: apiKeyField
                                Layout.fillWidth: true
                                echoMode: TextInput.Password
                                placeholder: "sk-..."
                            }

                            FieldLabel { text: "模型名" }
                            ThemedField {
                                id: modelField
                                Layout.fillWidth: true
                            }

                            FieldLabel { text: "Base URL" }
                            ThemedField {
                                id: baseUrlField
                                Layout.fillWidth: true
                            }

                            RowLayout {
                                Layout.topMargin: Theme.gapRelaxed
                                spacing: Theme.gapMd
                                FieldLabel { text: "Temperature"; Layout.topMargin: 0 }
                                Slider {
                                    id: tempSlider
                                    Layout.fillWidth: true
                                    from: 0.0; to: 2.0; stepSize: 0.1
                                    implicitHeight: 20
                                    // 对齐原型（settings-mockup/.slider）：4px 轨道(divider) + 14px 朱砂圆点
                                    // 背景：控制层会把 background 铺满控件，此处用透明底 + 内部细轨道，
                                    //       轨道宽=availableWidth，逐点居中对齐手柄的移动范围。
                                    background: Rectangle {
                                        color: "transparent"
                                        Rectangle {
                                            x: tempSlider.leftPadding
                                            width: tempSlider.availableWidth
                                            height: 4
                                            radius: 2
                                            color: Theme.divider
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }
                                    // 手柄：14px 朱砂圆点，垂直居中。用确定的 tempSlider.height/width，
                                    //       避免 parent.availableHeight（可为 NaN）与自引用 parent.handle.width。
                                    handle: Rectangle {
                                        width: 14
                                        height: 14
                                        radius: 7
                                        color: tempSlider.pressed ? Qt.lighter(Theme.accent, 1.12) : Theme.accent
                                        x: Math.round(tempSlider.leftPadding + tempSlider.visualPosition * (tempSlider.availableWidth - width))
                                        y: Math.round((tempSlider.height - height) / 2)
                                        Behavior on color { ColorAnimation { duration: Theme.animFast } }
                                    }
                                }
                                Label {
                                    text: tempSlider.value.toFixed(1)
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.sizeCaption
                                    color: Theme.textPrimary
                                    Layout.minimumWidth: 28
                                    horizontalAlignment: Text.AlignRight
                                }
                            }

                            Item { Layout.fillHeight: true }

                            RowLayout {
                                Layout.alignment: Qt.AlignRight
                                Layout.topMargin: Theme.gapRelaxed
                                spacing: Theme.gapSm
                                ThemedButton { text: "保存"; onClicked: modelsPage.save() }
                                ThemedButton {
                                    kind: "primary"
                                    text: "设为默认"
                                    onClicked: modelsPage.setDefault()
                                }
                            }
                        }
                    }

                    // 模型列表变化时刷新（增删/改名/默认切换）
                    Connections {
                        target: bridge
                        function onProvidersChanged() { modelsPage.reload() }
                    }
                }

                // ── 页 1: 项目 ──
                Rectangle {
                    id: projectsPage
                    color: "transparent"
                    property string selectedProject: ""

                    function reload() {
                        projModel.clear()
                        var list = bridge.allProjects()
                        var cur = bridge.projectPath
                        // 首次加载：优先选中当前项目，其次保持原选中
                        if (selectedProject === "")
                            selectedProject = cur
                        for (var i = 0; i < list.length; ++i) {
                            projModel.append({ title: list[i].title,
                                               path: list[i].path,
                                               isCurrent: list[i].isCurrent })
                            if (selectedProject === list[i].path)
                                selectedProject = list[i].path
                        }
                        // 选中项不在列表中（被删/不存在）→ 置空
                        var found = false
                        for (var j = 0; j < projModel.count; ++j) {
                            if (projModel.get(j).path === selectedProject) { found = true; break }
                        }
                        if (!found) selectedProject = ""
                    }

                    function openProjectAt(path) {
                        if (bridge.openProject(path)) {
                            root.close()
                            Toast.show("已打开项目")
                        }
                    }

                    ColumnLayout {
                        anchors { fill: parent; topMargin: Theme.gapSpacious; bottomMargin: Theme.gapSpacious; leftMargin: Theme.gapAmple; rightMargin: Theme.gapAmple }
                        spacing: 0

                        // 全部项目列表（对齐原型 .proj-list）
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: Theme.bgField
                            radius: Theme.radiusSm
                            border.width: 1
                            border.color: Theme.divider
                            clip: true

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 0

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 32
                                    Layout.leftMargin: Theme.gapCozy
                                    Layout.rightMargin: Theme.gapCozy
                                    Label {
                                        text: "全部项目（共 " + projModel.count + "）"
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.sizeCaption
                                        color: Theme.textFaint
                                        Layout.fillWidth: true
                                    }
                                }

                                // 空态：与列表同布局（对齐原型 .proj-empty；置于列表之前，空时可见）
                                // 无项目时同样保持「表头 + 内容区」单一区域，不额外插入分割线，
                                // 避免出现图一那种上下两半的割裂视觉 —— 与有项目时的版式保持一致。
                                Label {
                                    visible: projModel.count === 0
                                    text: "暂无项目，点击下方「新增」创建。"
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.sizeNote
                                    color: Theme.textFaint
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Layout.topMargin: 20
                                    Layout.leftMargin: 12
                                    horizontalAlignment: Text.AlignLeft
                                    verticalAlignment: Text.AlignTop
                                }

                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    visible: projModel.count > 0
                                    clip: true
                                    spacing: 4
                                    model: ListModel { id: projModel }
                                    ScrollBar.vertical: ScrollBar { width: 4; policy: ScrollBar.AsNeeded; background: Rectangle { color: "transparent" } }

                                    // 手风琴行：原型 .proj-row margin 2px 4px（水平 4 + 行序 2*2）
                                    delegate: Rectangle {
                                        required property string title
                                        required property string path
                                        required property bool isCurrent
                                        x: 4
                                        width: ListView.view.width - 8
                                        height: 36
                                        radius: Theme.radiusSm
                                        color: rowHover.hovered ? Theme.bgHover
                                             : (path === projectsPage.selectedProject ? Theme.accentTint : "transparent")

                                        Rectangle {
                                            visible: path === projectsPage.selectedProject
                                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; topMargin: 8; bottomMargin: 8 }
                                            width: Theme.markBar
                                            radius: Theme.markBar / 2
                                            color: Theme.accent
                                        }

                                        HoverHandler { id: rowHover }

                                        // 单击选中 / 双击打开
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                projectsPage.selectedProject = path
                                            }
                                            onDoubleClicked: projectsPage.openProjectAt(path)
                                        }

                                        RowLayout {
                                            anchors { left: parent.left; leftMargin: Theme.gapMd; right: parent.right; rightMargin: Theme.gapCozy; verticalCenter: parent.verticalCenter }
                                            spacing: Theme.gapSm
                                            Label {
                                                text: title
                                                font.family: Theme.fontUi
                                                font.pixelSize: Theme.sizeUi
                                                color: Theme.textPrimary
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                            // "当前"徽标：Rectangle 描边 + Label
                                            Rectangle {
                                                visible: isCurrent
                                                implicitWidth: curBadgeTxt.implicitWidth + 8
                                                implicitHeight: curBadgeTxt.implicitHeight + 2
                                                radius: Theme.radiusXs
                                                border.width: 1
                                                border.color: Theme.accent
                                                color: "transparent"
                                                Label {
                                                    id: curBadgeTxt
                                                    anchors.centerIn: parent
                                                    text: "当前"
                                                    font.family: Theme.fontUi
                                                    font.pixelSize: Theme.sizeMini
                                                    color: Theme.accent
                                                }
                                            }
                                            Label {
                                                visible: rowHover.hovered && !isCurrent
                                                text: "打开"
                                                font.family: Theme.fontUi
                                                font.pixelSize: Theme.sizeNote
                                                color: openHover.containsMouse ? Theme.accent : Theme.textSecondary
                                                MouseArea {
                                                    id: openHover
                                                    anchors.fill: parent
                                                    anchors.margins: -4
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: projectsPage.openProjectAt(path)
                                                }
                                            }
                                        }
                                    }
                                }

                            }
                        }

                        // 底部操作（对齐原型 .proj-page-actions）
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.gapMd
                            Layout.alignment: Qt.AlignRight
                            spacing: Theme.gapSm
                            ThemedButton {
                                kind: "danger"
                                text: "删除"
                                enabled: projectsPage.selectedProject !== ""
                                onClicked: {
                                    var name = ""
                                    for (var i = 0; i < projModel.count; ++i) {
                                        if (projModel.get(i).path === projectsPage.selectedProject) {
                                            name = projModel.get(i).title
                                            break
                                        }
                                    }
                                    confirmDlg.titleText = "删除项目"
                                    confirmDlg.messageText = "确定要删除项目「%1」吗？\n仅从项目列表中移除，项目文件仍保留在磁盘上。"
                                    confirmDlg.detailName = name
                                    confirmDlg.open()
                                }
                            }
                            ThemedButton {
                                kind: "primary"
                                text: "新增"
                                onClicked: createProjectDialog.open()
                            }
                        }
                    }
                }

                // ── 页 2: 调试 ──
                ColumnLayout {
                    anchors.margins: Theme.gapSpacious
                    RowLayout {
                        Label {
                            text: "启用调试日志"
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.sizeUi
                            color: Theme.textPrimary
                            Layout.fillWidth: true
                        }
                        ThemedSwitch {
                            checked: bridge.verboseEnabled()
                            onToggled: bridge.setVerbose(checked)
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }

    // 新建项目弹窗（共享）
    CreateProjectDialog {
        id: createProjectDialog
        onProjectCreated: projectsPage.reload()
    }

    // 删除确认（软删语义）
    ConfirmDialog {
        id: confirmDlg
        onConfirmed: {
            if (bridge.deleteProject(projectsPage.selectedProject)) {
                projectsPage.reload()
                Toast.show("已删除项目")
            }
            // 失败时 C++ 已 emit uiErrorOccurred → MainWindow 全局 Toast 提示，
            // 此处不再重复提示，仅保持列表不刷新（软删未发生的原状）
        }
    }

    // 项目变化（打开/创建/删除）时刷新列表
    Connections {
        target: bridge
        function onProjectChanged() { projectsPage.reload() }
    }
}
