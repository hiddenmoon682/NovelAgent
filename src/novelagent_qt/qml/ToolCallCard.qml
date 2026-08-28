import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ToolCallCard — 工具调用折叠条：与"思考过程"卡片逐参数同构（对照
// ChatBubble.qml:57-125）：纯文本标题条 + ▸/▾ + 点击展开，展开区为
// 左竖线 + 单一弱化小字 Text。工具名直接拼在折叠条标题上
//（"工具调用 · write_chapter"），展开正文只含状态/参数/结果。
// status: "running" | "ok" | "error"；展开态由模型条目 toolExpanded 驱动
// （防 delegate 回收复用后状态错乱），经 expandedToggled 回写。
ColumnLayout {
    id: root

    property string toolName: ""
    property string status: "running"
    property string toolArgs: ""
    property string toolResult: ""
    property bool expanded: false

    signal expandedToggled

    width: parent ? parent.width : 0
    spacing: Theme.gapXs

    // JSON 原文 → 美化缩进；解析失败（非 JSON 文本）原样返回。
    function prettyJson(src) {
        if (!src || src.length === 0) return ""
        try { return JSON.stringify(JSON.parse(src), null, 2) }
        catch (e) { return src }
    }

    // 状态后缀（拼在折叠条标题末尾，纯文本与标题同款式）
    readonly property string statusSuffix: root.status === "running" ? " · 执行中…"
                                           : root.status === "ok" ? " · 完成" : " · 失败"

    // 展开正文组装：参数/结果（工具名与状态已在折叠条标题上，不再重复；
    // 正文与思考过程同字号同色）
    function detailText() {
        var lines = []
        if (root.toolArgs.length > 0)
            lines.push("参数：\n" + root.prettyJson(root.toolArgs))
        if (root.toolResult.length > 0)
            lines.push("结果：\n" + root.prettyJson(root.toolResult))
        if (lines.length === 0)
            lines.push("暂无参数与结果详情")
        return lines.join("\n\n")
    }

    // ── 折叠条（与"思考过程"同参：标题 textFaint sizeCaption + ▸/▾；
    //    height24 / radiusSm / hover 渐变 / AlignLeft / leftMargin gapSm）──
    Rectangle {
        Layout.alignment: Qt.AlignLeft
        Layout.leftMargin: Theme.gapSm
        width: headerRow.implicitWidth + Theme.gapMd * 2
        height: 24
        radius: Theme.radiusSm
        color: barMa.containsMouse ? Theme.bgHover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.animFast } }

        Row {
            id: headerRow
            anchors.centerIn: parent
            spacing: Theme.gapXs

            Label {
                // 纯文本标题（与"思考过程"同形）：工具名与状态直接拼在标题上，
                // 一眼可见调用了哪个工具、进行到哪一步（统一 sizeCaption/textFaint）
                text: root.toolName.length > 0
                      ? "工具调用 · " + root.toolName + root.statusSuffix
                      : "工具调用" + root.statusSuffix
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textFaint
            }
            Label {
                text: root.expanded ? "\u25be" : "\u25b8"
                font.pixelSize: Theme.sizeCaption
                color: Theme.textFaint
            }
        }

        MouseArea {
            id: barMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.expandedToggled()
        }
    }

    // ── 展开详情（与"思考过程"展开区同构：左竖线 + 单一 Text 弱化小字）──
    Rectangle {
        visible: root.expanded
        Layout.leftMargin: Theme.gapSm
        Layout.preferredWidth: root.width * 0.82
        implicitHeight: detailTextItem.implicitHeight + Theme.gapSm * 2
        color: "transparent"

        Rectangle {
            width: 2
            height: parent.height
            color: Theme.divider
        }

        Text {
            id: detailTextItem
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                leftMargin: Theme.gapMd
                topMargin: Theme.gapSm
            }
            text: root.detailText()
            wrapMode: Text.Wrap
            font.family: Theme.fontUi
            font.pixelSize: Theme.sizeCaption + 1
            color: Theme.textFaint
            // 参数 JSON 可能极长（如 update_chapter 携带全文）：行数截断防止
            // 展开区撑爆视口（样式与思考过程一致，截断仅保护内容量级）
            maximumLineCount: 12
            elide: Text.ElideRight
        }
    }
}