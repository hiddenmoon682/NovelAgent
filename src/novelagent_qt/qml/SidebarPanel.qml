import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// SidebarPanel — 左侧栏：应用标题 + 当前项目卡片 + 会话列表 + 设置入口。
Rectangle {
    id: root
    color: Theme.bgSidebar

    // 点击设置齿轮时发射，由 MainWindow 打开设置对话框
    signal settingsRequested()

    // 最近项目列表展开态（手风琴）：点击卡片切换，选中项目后自动收起
    property bool projectListOpen: false

    // 展开时刷新：每次从 bridge 重新拉取，避免过期数据与已删除记录残留
    function reloadProjects() {
        projModel.clear()
        var list = bridge.recentProjects()
        for (var i = 0; i < list.length; ++i) {
            projModel.append({ title: list[i].title, path: list[i].path,
                               isCurrent: list[i].isCurrent })
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── 当前项目卡片（可点击：跳转项目设置）──
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapSm
            Layout.rightMargin: Theme.gapSm
            Layout.topMargin: Theme.gapMd
            // 展开时贴合下拉面板（原型 margin-top 6px），收起时保持与下方分割线的呼吸
            Layout.bottomMargin: root.projectListOpen ? 6 : Theme.gapMd
            height: 56
            radius: Theme.radiusMd
            color: projectMa.containsMouse ? Theme.bgHover : Theme.bgElevated
            border.width: 1
            border.color: Theme.divider
            Behavior on color { ColorAnimation { duration: Theme.animFast } }

            MouseArea {
                id: projectMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    root.projectListOpen = !root.projectListOpen
                    if (root.projectListOpen)
                        root.reloadProjects()
                }
            }

            ColumnLayout {
                // 文本块垂直居中；不撑满卡片高度，避免行盒内边距被放大
                anchors {
                    left: parent.left; right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: Theme.gapMd; rightMargin: Theme.gapMd
                }
                spacing: 1
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gapSm
                    Label {
                        text: "当前项目"
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.sizeNote
                        // 13px 级字体的行盒上下各含约 3px 内边距，两行叠放视觉间隙
                        // 偏大；0.9 行高压掉冗余后与 1px 间距叠加约 3px
                        lineHeight: 0.9
                        color: Theme.textFaint
                        Layout.fillWidth: true
                    }
                    // ▾ 展开箭头：随展开态旋转 180°
                    Label {
                        text: "\uE70D"  // Segoe MDL2 Assets ChevronDown
                        font.family: "Segoe MDL2 Assets"
                        font.pixelSize: 20
                        color: Theme.textFaint
                        rotation: root.projectListOpen ? 180 : 0
                        Behavior on rotation { NumberAnimation { duration: Theme.animFast } }
                    }
                }
                Label {
                    text: bridge.projectName
                    font.family: Theme.fontDisplay
                    font.pixelSize: Theme.sizeUi
                    font.weight: Font.DemiBold
                    lineHeight: 0.9
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }

        // ── 最近项目展开面板（手风琴）：整块圆角卡片（原型 .proj-panel）──
        Rectangle {
            id: projPanel
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapSm
            Layout.rightMargin: Theme.gapSm
            visible: root.projectListOpen
            radius: Theme.radiusMd
            color: Theme.bgElevated
            border.width: 1
            border.color: Theme.divider

            ColumnLayout {
                id: panelCol
                anchors { left: parent.left; right: parent.right; top: parent.top }
                anchors.margins: Theme.gapXs
                spacing: 2

            ListView {
                id: projList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 260)
                clip: true

                model: ListModel { id: projModel }

                // 细滚动条：4px 圆角 thumb，主题色，不占视觉宽度
                ScrollBar.vertical: ScrollBar {
                    width: 4
                    policy: ScrollBar.AsNeeded
                    background: Rectangle { color: "transparent" }
                    contentItem: Rectangle {
                        radius: 2
                        color: parent.pressed || parent.hovered ? Theme.textFaint : Theme.divider
                    }
                }

                delegate: Rectangle {
                    required property string title
                    required property string path
                    required property bool isCurrent
                    width: projList.width
                    height: 34
                    radius: Theme.radiusSm
                    color: projRowHover.hovered ? Theme.bgHover
                         : (isCurrent ? Theme.accentTint : "transparent")

                    // 当前项目：左侧朱砂标条
                    Rectangle {
                        visible: isCurrent
                        anchors { left: parent.left; top: parent.top; bottom: parent.bottom; topMargin: Theme.gapTight; bottomMargin: Theme.gapTight }
                        width: Theme.markBar
                        radius: Theme.markBar / 2
                        color: Theme.accent
                    }

                    HoverHandler { id: projRowHover }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (isCurrent) {
                                root.projectListOpen = false
                                return
                            }
                            if (bridge.openProject(path))
                                root.projectListOpen = false
                        }
                    }

                    RowLayout {
                        anchors { fill: parent; leftMargin: Theme.gapTight; rightMargin: Theme.gapSm }
                        spacing: Theme.gapSm
                        Label {
                            text: title
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.sizeUi
                            color: isCurrent ? Theme.accent : Theme.textPrimary
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        // 垃圾桶：悬停行时出现（Segoe MDL2 Assets U+E74D Delete）
                        Label {
                            text: "\uE74D"
                            visible: projRowHover.hovered
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 14
                            color: delMa.containsMouse ? Theme.warning : Theme.textFaint
                            MouseArea {
                                id: delMa
                                anchors.fill: parent
                                anchors.margins: -6
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (bridge.removeRecentProject(path))
                                        root.reloadProjects()
                                }
                            }
                        }
                    }
                }
            }

            // 空态：没有任何最近项目（对齐原型"暂无最近项目"）
            Label {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                visible: projModel.count === 0
                text: "暂无最近项目"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeNote
                color: Theme.textFaint
                verticalAlignment: Text.AlignVCenter
                leftPadding: Theme.gapTight
            }

            // 面板分隔线（原型 .panel-divider margin 4px 0，空态也保留）
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                Layout.topMargin: Theme.gapXs
                Layout.bottomMargin: Theme.gapXs
                color: Theme.divider
            }

            // 常驻入口：添加项目（对齐原型 app-mockup；创建后自动收起面板）
            MouseArea {
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: createProjectDialog.open()
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSm
                    color: parent.containsMouse ? Theme.bgHover : "transparent"
                    RowLayout {
                        anchors { fill: parent; leftMargin: Theme.gapMd }
                        spacing: Theme.gapTight
                        Label {
                            text: "＋"
                            font.family: Theme.fontUi
                            font.pixelSize: 14
                            color: Theme.accent
                        }
                        Label {
                            text: "添加项目"
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.sizeNote
                            color: parent.containsMouse ? Theme.textPrimary : Theme.textSecondary
                        }
                    }
                }
            }

            // 新建项目弹窗（创建成功后收起面板）
            CreateProjectDialog {
                id: createProjectDialog
                onProjectCreated: {
                    root.projectListOpen = false
                    root.reloadProjects()
                }
            }
            }

            // 高度跟随内部内容（ColumnLayout 未锚定底部 → 高度=内容 implicit）
            height: panelCol.implicitHeight + Theme.gapXs * 2
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

        // ── 会话标题行 ──
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.gapLg
            Layout.rightMargin: Theme.gapSm
            Layout.topMargin: Theme.gapMd
            Layout.bottomMargin: Theme.gapSm

            Label {
                text: "会话"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                font.weight: Font.DemiBold
                color: Theme.textSecondary
                Layout.fillWidth: true
            }
            Button {
                id: newSessionBtn
                text: "+ 新建"
                onClicked: bridge.createPoolSession()
                contentItem: Text {
                    text: newSessionBtn.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeCaption
                    color: Theme.accent
                }
                background: Rectangle {
                    radius: Theme.radiusSm
                    color: newSessionBtn.hovered ? Theme.bgHover : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }
            }
        }

        // ── 会话列表（来自 bridge.sessionList()，按最近使用降序）──
        ListView {
            id: sessionList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            leftMargin: Theme.gapSm
            rightMargin: Theme.gapSm
            spacing: 2

            model: ListModel { id: sessionsModel }

            // 空状态占位：无会话时给出引导，避免大片空白
            Label {
                anchors.centerIn: parent
                visible: sessionsModel.count === 0
                text: "暂无会话\n点击右上角「+ 新建」开始创作"
                horizontalAlignment: Text.AlignHCenter
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeUi
                lineHeight: 1.7
                color: Theme.textFaint
            }

            function reload() {
                sessionsModel.clear()
                if (!bridge.agentReady) return
                var list = bridge.sessionList()
                for (var i = 0; i < list.length; ++i) {
                    sessionsModel.append({ sid: list[i].id, name: list[i].title,
                                           active: list[i].active,
                                           running: list[i].running === true })
                }
            }
            Component.onCompleted: reload()

            delegate: Rectangle {
                width: sessionList.width - Theme.gapSm * 2
                height: 36
                radius: Theme.radiusSm
                color: model.active || rowHover.hovered ? Theme.bgHover : "transparent"

                // HoverHandler 不与 MouseArea 互斥，悬停时同时高亮行 + 显示删除按钮
                HoverHandler { id: rowHover }

                // 整行点击切换会话（删除按钮的 MouseArea 在其上层，不受影响）
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (!model.active) bridge.switchPoolSession(model.sid)
                }

                RowLayout {
                    anchors { fill: parent; leftMargin: Theme.gapMd; rightMargin: Theme.gapMd }
                    spacing: Theme.gapSm
                    Rectangle {
                        width: 7; height: 7; radius: 3.5
                        color: model.running ? Theme.warning
                                             : (model.active ? Theme.agentTint : Theme.textFaint)
                        // 运行中会话用警示色圆点提示（阶段 4）
                        Rectangle {
                            visible: model.running
                            anchors.fill: parent
                            radius: 3.5
                            color: "transparent"
                            border.color: Theme.warning
                        }
                    }
                    Label {
                        text: model.name
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.sizeUi
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    // 删除按钮：悬停行时可见；删除 active 会话后自动切到最近会话
                    Label {
                        text: "\u00d7"
                        visible: rowHover.hovered
                        font.pixelSize: 15
                        color: deleteMa.containsMouse ? Theme.warning : Theme.textFaint

                        MouseArea {
                            id: deleteMa
                            anchors.fill: parent
                            anchors.margins: -6
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: bridge.deleteSession(model.sid)
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

        // ── 底部工具栏：版本号 + 重建索引 + 设置入口 ──
        Rectangle {
            Layout.fillWidth: true
            height: 44
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.gapLg
                anchors.rightMargin: Theme.gapMd
                spacing: Theme.gapSm

                // 左侧利用空位弱化展示版本号
                Label {
                    text: "v" + Qt.application.version
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.sizeCaption
                    color: Theme.textFaint
                }

                Item { Layout.fillWidth: true }

                // 重建索引：强制全量重嵌入（索引损坏/换嵌入模型后的自愈入口）
                Rectangle {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                radius: Theme.radiusSm
                color: rebuildMa.containsMouse && !bridge.busy ? Theme.bgHover : "transparent"
                opacity: bridge.busy ? 0.4 : 1.0

                ToolTip.visible: rebuildMa.containsMouse
                ToolTip.text: bridge.busy ? "Agent 正忙，稍后重试" : "重建向量索引"
                ToolTip.delay: 300

                Label {
                    anchors.centerIn: parent
                    text: "\u27f3"
                    font.pixelSize: 16
                    color: rebuildMa.containsMouse && !bridge.busy ? Theme.textPrimary : Theme.textSecondary
                }

                MouseArea {
                    id: rebuildMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: bridge.busy ? Qt.ArrowCursor : Qt.PointingHandCursor
                    onClicked: if (!bridge.busy) bridge.rebuildIndex()
                }
            }

                Rectangle {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    radius: Theme.radiusSm
                    color: settingsMa.containsMouse ? Theme.bgHover : "transparent"

                ToolTip.visible: settingsMa.containsMouse
                ToolTip.text: "设置"
                ToolTip.delay: 300

                Label {
                    anchors.centerIn: parent
                    text: "\u2699\uFE0E"
                    font.pixelSize: 18
                    color: settingsMa.containsMouse ? Theme.textPrimary : Theme.textSecondary
                }

                MouseArea {
                    id: settingsMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.settingsRequested()
                }
                }
            }
        }
    }

    // 会话列表随后端变化刷新（新建/切换/删除/标题自动提取/Agent 重建）
    Connections {
        target: bridge
        function onSessionsChanged() { sessionList.reload() }
        function onAgentReadyChanged() { sessionList.reload() }
        // 项目变化（打开/创建/软删/切换）时若面板展开则刷新最近列表
        function onProjectChanged() {
            if (root.projectListOpen)
                root.reloadProjects()
        }
    }
}
