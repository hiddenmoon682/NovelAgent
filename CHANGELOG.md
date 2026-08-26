# Changelog

## [2026-08-26] 设置页「调试」页内容边距修正

### 修复 — GUI
- **调试页「启用调试日志」顶满左上、无左右留白**：该页 `ColumnLayout` 此前只写 `anchors.margins: Theme.gapSpacious` 而未 `fill: parent`，边距不生效，内容贴住容器左上、右侧无留白。改为与「模型」「项目」页一致：`anchors { fill: parent; topMargin/bottomMargin: Theme.gapSpacious; leftMargin/rightMargin: Theme.gapAmple }`，使「启用调试日志」上下/左右都留出边距（Fusion 迁移走查时发现）。

## [2026-08-26] Qt Quick Controls 样式切换：Material → Fusion

### 变更 — GUI / 样式层
- **弃用 Material，改用 Fusion**：`QmlApp.cpp` 的 `QT_QUICK_CONTROLS_STYLE` 改 `Fusion`，并新增应用级深色 `QPalette`（窗口/基础/文字/高亮等色值与 `Theme.qml` 对齐，高亮=朱砂 `#c9553e`），替代原 `Material.theme: Dark`。`MainWindow.qml` 删除 Material 附加上下文两行。背景依据：Material 样式会在自定义 background/圆角/padding 下**动态推值**（`TextArea.topPadding` 被算成负值致占位符/内容/光标飞出框外、默认遮罩偏白、Button inset 内缩等），与自绘控件反复冲突；Fusion 对自定义 background/圆角/padding 完全可预测，切后一次清掉这类「Material 跟自定义打架」的坑。
- **保留并在 Fusion 下复验**：`CreateProjectDialog` 简介框的显式 per-side padding 与占位符锚定 `background` 方案在 Fusion 下依然正确；主窗口/设置弹窗/新建项目弹窗均以深色暖墨渲染正常，占位符与输入光标落在框内。

## [2026-08-26] 新建项目弹窗占位符：组合态重叠 + 简介占位符溢出

### 修复 — GUI
- **输入内容时占位符不消失、与正文重叠（图一/图二）**：占位符可见条件此前绑定 `text === ""`。中文输入法（拼音/五笔等）组合态上屏中，输入框显示的是组合文（`preeditText`），而 `text`/`length` 仍为空——条件恒真，占位符一直显示，与正文重叠。改为对齐 Qt 原生占位符可见条件 `length === 0 && preeditText.length === 0`（`ThemedField.qml` 与 `CreateProjectDialog.qml` 的 descArea），组合态一输入即隐藏，杜绝重叠。
- **小说简介占位符/输入光标「飞出框外」**：根因是 Material 样式下 `TextArea.topPadding` 会被算成 **负值**。Material 样式模板用 `topPadding = (implicitBackgroundHeight - placeholder.largestHeight)/2 + topInset` 动态推 padding，而本项目自定义的 `background: Rectangle` 没有 `implicitHeight`，使 `implicitBackgroundHeight = 0`，`topPadding` 变负——占位符/内容/输入光标（content 起点）按该 y 渲染就飘到框上方。修法两点：① 把占位符改作 **可见框 `background` 的子项、锚定左上**（`anchors.left/top` + `width` + `elide`），不再依赖控件 `topPadding`；② descArea 改用**显式 per-side padding**（`left/top/right/bottomPadding`）覆盖 Material 动态 `topPadding` 的负值，使内容与输入光标从框内 `(12,10)` 起渲染、与占位符对齐。标题框（`ThemedField`）因单行 + `verticalAlignment: AlignVCenter`、其 Material `topPadding` 本就是固定正值，故光标居中在框内、无此问题。

## [2026-08-26] 项目页空态版式对齐有项目态

### 修复 — GUI
- **设置页「项目」无项目时呈上下两半割裂（图一）**：空态在「全部项目（共 N）」表头与空态文案之间插入了一条整宽分割线（`Rectangle { height: 1; color: Theme.divider }`），视觉上把面板劈成上下两半；而有项目时（图二）没有该分割线，直接是「表头 + 内容区」。删除该分割线，使空态与有项目态保持同一「表头 + 内容区」版式；空态文案改为顶对齐并填充内容区，落在列表首行应处的位置。

## [2026-08-26] 软删目录重名判定修复

### 修复 — C++
- **删掉项目后无法再以同名创建（"同名项目已存在"）**：`createProjectAt` 重名判定枚举 `listProjects`（含软删目录）后直接 `peekTitle` 比对，而软删目录（目录名带"（已删除）"）在列表中被过滤、用户不可见，却仍占着标题挡住同名再创建。重名判定循环补 `isSoftDeleted` 跳过（`allProjects` 早已过滤，仅此处在创建路径漏了）。

## [2026-08-26] 删除项目软删修复 + Toast/确认弹窗健壮性

### 修复 — C++
- **软删失败（"删除失败（目录重命名出错）"）根因**：`deleteProject` 原顺序是**先 `fs::rename` 目录、后 `rebuildApp`**——当前项目被删除时 Agent/SqliteStore 仍持有项目内 `novel.db` 等文件句柄，Windows 上重命名含被打开文件的目录必失败。改为**先重建为无项目状态（释放句柄）、再重命名目录**；非当前项目删除不受影响。错误信息附带 `ec.message()` 便于诊断。

### 修复 — GUI
- **删除确认弹窗显示字面量「%1」**：`ConfirmDialog` 替换逻辑依赖 `onTextChanged` 事件，但 `SettingsDialog` 先设 `messageText` 后设 `detailName`——text 变化时 detailName 尚空、替换被跳过，`Component.onCompleted` 又绑回原文本。改为纯绑定计算（`text:` 内联替换 + `escapeHtml` 防御转义），不再依赖事件时序。
- **Toast QML 警告（Toast.qml:15 读取 null.width）**：单例 QtObject 上下文里 `Overlay.overlay` attached property 求值为 null（历史 QML 警告根因）；`show()` 在调用点取 overlay、防御性判空返回，popup 的 x/y 改用 `pop.parent` 定位。删除失败触发 Toast 时不再崩溃，失败原因（如"Agent 正在生成中"）能正常显示。

## [2026-08-26] UI 操作失败与聊天错误通道分离（C++ + QML）

### 修复 — GUI
- **删除/创建/打开项目失败不再弹进对话区**：UI 操作失败此前全部复用 `errorOccurred`（聊天错误通道），被 `AgentPanel` 当作聊天消息「⚠ 删除失败...」追加进对话记录（原型里提示应就地显示，不该污染聊天流）。新增专用信号 `uiErrorOccurred(QString)` 承接 UI 操作失败（删除/创建/打开项目、Provider 删除/初始化、重建索引前置/失败、读章节失败、技能切换），`MainWindow` 全局接该信号 → `Toast` 顶部轻提示；`errorOccurred` 只保留聊天/Agent 运行时错误（发送消息前置、会话操作、runAgent 异常、自动索引、并发/上下文溢出）。

## [2026-08-26] 修复中文路径文件读写崩溃（C++）

### 修复 — C++
- **创建中文项目名直接崩溃（"无法创建临时文件: ...<乱码>.tmp.0"）**：Windows（MSYS2 MinGW GCC）上 `std::ifstream`/`std::ofstream` 的窄 `std::string` 构造按**系统 ANSI 代码页**（本机 GBK/936）解释路径，而源码/路径是 UTF-8——中文路径打开失败（目录用 `std::filesystem` 建成功、窄流入流失败，即编码不一致）。修法：`FileUtils::readText`/`writeText` 与 `SqliteStore::hasValidSqliteHeader` 从窄 string 构造流时先包成 `std::filesystem::path` 变量（不可直接 `ofstream(fs::path(p))`，会触发 most-vexing-parse）；其余传 `fs::path` 表达式的调用（`BuiltinSkills`/`SkillLoader`/`SkillTools`）走 C++17 path 重载本就安全。

## [2026-08-25] QML 修复与规范

### 修复 — GUI
- **模型列表整列白块（顽疾根因）**：delegate 颜色绑定引用未定义 id `rowHover`（应为 `modelRowHover`）→ 绑定静默失效、`Rectangle.color` 取默认白色；修正引用后恢复深色主题渲染。
- **侧边栏项目展开面板文字叠字**：卡片 `Rectangle` 无高度绑定致塌陷为 0，内容溢出叠画；改显式高度跟随内部布局内容。
- **设置弹窗项目空态 QML 警告**：布局容器子项误用 `anchors.centerIn`（undefined behavior），改 `Layout.*` + 文本对齐。
- **设置弹窗左 rail 导航重叠**：`Row` + 整行宽 `Item`（`width: railRow.width`）令「模型/项目/调试」横排溢出——仅第一项可见，第二项"项"字戳进右侧「模型列表」表头造成控件重叠与表头文字错乱；改 `Column` 竖直堆叠（对照 `期望效果图-3`）。
- **模型页新增 Provider 标识字段（对齐 `期望效果图-3`）**：把「命名」降级为仅编辑**显示名**，新增只读的 **Provider** 字段展示稳定标识（map 键）。`QmlBridge::providerInfo` 的 `isDefault` 改用标识（键）判定、`saveProvider` 的 `rename_to` 不再做 map 键迁移（重命名不再改动 default_provider 引用与运行中 Agent 的 provider 名），列表项 `title`=显示名、`key`=标识（QML 中 `id` 为保留关键字，故角色名用 `key`）。
- **对齐原型间距**（对照三组期望/实际截图）：设置弹窗去标题区（rail 通顶）、rail 选中底改 `bgElevated`、内容区左右 20、两列表头 32、模型行右侧 10、滑块手柄 14px、滑块行间距 12、项目行手风琴间距（36 高 + 4px 边距）、「命名」字段顶距归零、新建弹窗标题上距 18 / 内容底 12 / 错误行零占位、确认弹窗底部 12、侧边栏面板整块卡片化（radius 10、padding 4、上距 6）、chevron 20px、「＋」14px、面板分隔线恒显。
- **侧边栏底部工具栏布局化**：消除 `anchors.rightMargin: gapMd + 32 + gapSm` 手工推算坐标，改 `RowLayout` + 弹性空位。
- **ThemedField 占位符重做**：放弃 TextField 默认 `placeholderText` 机制（其在密码 `echoMode` + 本项目字体垂直对齐下出现「占位符与内容重叠、不随内容隐藏、竖直溢出控件边界」的缺陷），改为自绘占位 `Label`——仅 `text==""` 时显示、垂直居中、宽度=内容区并省略、不拦截鼠标；调用方属性由 `placeholderText` 改为 `placeholder`（`SettingsDialog`/`WelcomeWizard`/`CreateProjectDialog`）。
- **Temperature 滑块圆形手柄飞出**：`handle` 的 `y` 用 `parent.availableHeight`（可为 NaN）、`x` 用自引用 `parent.handle.width`，导致手柄错位/飞出；改为确定的 `tempSlider.height`/`width`，并补一条 **4px `divider` 轨道**（对齐 `settings-mockup.html` 的 `.slider`）。
- **模态遮罩偏白**：`Overlay.modal` 改 `Qt.rgba(0,0,0,0.55)`（对齐 `app-mockup.html` 的 `rgba(0,0,0,0.55)`，避免十六进制 alpha 位误解析），并给遮罩矩形加 `anchors.fill: parent`——否则自定义矩形默认尺寸 0×0 不铺满窗口，弹窗周围无降暗（历史问题）。
- **新建弹窗「小说简介」TextArea 占位符**：同 ThemedField 缺陷（默认占位符错位/溢出），改手动占位 `Label`（`text==""` 时显示、置左上、不拦截鼠标）。
- **ThemedSwitch 对齐 mockup**：`app-mockup.html` 的 `.switch` 为 40×22 圆角轨（off=divider/on=accent）+ 18px 圆点（off=text-secondary/on=#f5efe2）；原 36×20/14px 不符，已按 mockup 重做。
- **设置弹窗「项目」页对齐 `app-mockup.html`**（按需求完全对齐）：去掉「固定目录…/在资源管理器中打开」行与「打开其他目录中的项目…」弱入口，只保留项目列表（置顶左对齐空态 + 行选中朱砂标条/当前徽标/悬停打开）+ 右下角「删除（未选中置灰）/新增」；顺带移除已无引用的 `openFolderDlg`（`FolderDialog`）与未用 `import QtQuick.Dialogs`。
- **模态遮罩仍偏白（根因修正）**：Qt 6 中 `Overlay.modal`/`Overlay.modeless` 只能挂在 Popup 上（官方文档：The property can be attached to any popup），此前挂在 `MainWindow`（ApplicationWindow）上的遮罩配置静默无效——白色即 Material 默认偏浅遮罩；新建共享组件 `ModalDimmer`（色值取 `Theme.overlayDim`，60% 黑），由 `SettingsDialog`/`WelcomeWizard`/`CreateProjectDialog`/`ConfirmDialog` 四个模态弹窗以 `Overlay.modal: ModalDimmer {}` 引用。

### 规则
- `CLAUDE.md` 新增「QML 专项规则」：**布局优先**（禁止 anchors + 数学表达式定位；布局容器子项禁用 anchors；布局内 Rectangle 必须显式高度）、**QML 警告零容忍**（引用未定义 id = 绑定静默失效取默认值，验收须日志无警告）、**样式统一从 Theme 档位取**。

## [2026-08-25] QML 前端按 HTML 原型重构

### 增强 — GUI
- **设置弹窗左右式重构**：560×540 顶部选项卡 → 780×520 左侧 rail 导航（模型/项目/调试），rail 项 36px 高、选中朱砂底 + 2.5px 标条；对齐 `settings-mockup.html`。
- **模型页多模型管理**：左侧模型列表（＋ 新增"未命名"/悬停 ✕ 删除/「默认」徽标/朱砂选中态）+ 右侧表单（命名可改名/API Key/模型名/Base URL/Temperature 滑块）；「设为默认」即保存 + 启用当前模型。
- **项目页全部项目列表**：枚举固定目录（过滤软删目录），recent 排序在前；行单击选中/双击或悬停「打开」进入；右下角「新增」打开新建弹窗、「删除」走确认弹窗（软删）；固定目录路径展示 + 「在资源管理器中打开」；保留弱化入口"打开其他目录中的项目…"（兼容旧目录项目）。
- **新建项目弹窗（新 CreateProjectDialog）**：对齐 `create-project-mockup.html`——固定目录创建（`~/.novelagent/projects`）、只填书名+简介（简介 140px 高多行）、就地校验（非法字符/重名红字、错误行零占位不抖动静）、Enter 创建、成功后 toast 提示并自动进入。
- **Toast 全局提示（新 Toast singleton）**：顶部居中、1600ms 自动消失、懒加载；覆盖新建/删除/保存模型等操作反馈。
- **ConfirmDialog（新通用确认弹窗）**：380 宽、宋体标题、正文朱砂高亮项目名；删除确认文案明确"仅从列表移除，文件保留在磁盘"。
- **侧边栏**：手风琴底部「打开其他项目…」→「＋ 添加项目」常驻入口（创建后自动收起）；项目行选中底改 `accentTint` 半透明朱砂、标条改 `Theme.markBar`；空态改"暂无最近项目"；`projectChanged` 时展开态自动刷新。
- **WelcomeWizard 适配固定目录**：第 2 步新建区去掉目录浏览，改提示"项目将保存到固定目录"+「新建项目…」打开共享弹窗。
- **Theme 扩展**：新增 `accentTint`（选中行底）、`markBar`（2.5px 标条）、`sizeMini`（徽标）、`radiusXs/radiusToast`、间距细档位 `gapMicro~gapAmple`。

### 变更 — C++（QmlBridge 扩展）
- 新增 `projectsDir()`：固定项目根目录 `~/.novelagent/projects`。
- 新增 `createProjectAt(title, desc)`：固定目录创建，返回状态码（ok/invalid_title/invalid_chars/duplicate/failed）；重名大小写不敏感判重，目录名冲突自动追加 `-2` 后缀。
- 新增 `allProjects()`：枚举固定目录 − 软删目录，按 recent 排序。
- 新增 `deleteProject(path)`：**软删**——目录重命名为「原名（已删除）」并从 recent 移除、清 `last_project_path`；删当前项目时 Agent 重建为无项目状态，重建失败回滚重命名。磁盘内容保留。
- 新增 `addProvider()` / `deleteProvider(name)`：多模型管理（默认/运行中模型拒绝删除）；`saveProvider` 支持 `rename_to` 改名并同步 `default_provider`。
- `ProjectManager::create` 新增 description 重载；新增 `isSoftDeleted(path)` 静态谓词。
- 修复既有 bug：`rebuildApp` 重建后清空 `current_session_id_`/`recent_sessions_`（否则切换项目后首条消息报"会话不存在"）。
- QmlApp 连接 QML 引擎 `warnings` 信号输出加载错误日志。

### 测试
- `test_project_io` 新增：create 带简介持久化、`getDefaultProjectDir` 目录名安全化、软删目录标记识别与重命名冲突后缀。

## [2026-08-22] 侧边栏"当前项目"卡片与下拉面板间隙收紧

### 变更 — GUI
- 展开最近项目下拉面板时，"当前项目"卡片与面板间距由 16px（卡片下边距 `gapMd` 12px + 面板上边距 `gapXs` 4px）收窄为 6px：卡片下边距在展开态改为 6px、面板上边距归零，与原型 `recent-project-mockup.html`（`.proj-panel { margin-top: 6px }`）一致；收起态卡片下边距保持 `gapMd`(12px) 不变。

## [2026-08-21] 侧边栏最近项目下拉列表

### 增强 — GUI
- "当前项目"卡片改造为手风琴式最近项目列表：点击卡片展开/收起（▾ 箭头旋转），展开时展示全部历史打开过的项目（去重置顶、不设上限），点击某条立即切换并自动收起，当前项目行朱砂色 + 左侧标条高亮；行内不显示路径。
- 悬停条目右侧出现垃圾桶图标（Segoe MDL2 U+E74D），点击从最近列表移除该记录；列表内部 4px 细滚动条，空数据时显示"打开或创建项目…"入口，底部常驻"打开其他项目…"（保持原卡片跳设置项目页行为）。
- 持久化：`config.json` 新增 `recent_projects` 数组（键缺失或为 null 时取空列表），`openProject`/`createProject` 成功后记录；`ProjectIO::peekTitle` 轻量读取项目标题用于展示。

## [2026-08-21] 侧边栏"当前项目"卡片排版调整

### 变更 — GUI
- 标题栏底部补 1px 分割线：标题栏与侧边栏同底色（`bgSidebar`），左上区域无边界线，"卡片距标题栏"的间隙无可参照边界、感知上大于实际；补线后标题栏线→卡片(12px)与卡片→分割线(12px)有对称边界参照。
- 卡片上下边距统一为 `gapMd`(12px)：原上边距 `gapLg`(16px) 大于下边距，改为一致，且与分割线下方"会话"区(12px)保持统一节奏。
- "当前项目"标签字号 `sizeCaption`(11px)→ 新增 `sizeNote`(12px)：11px 过小、13px 时雅黑字面比下方 13px 衬线项目名还显大，12px 与下方项目名(13px 衬线 DemiBold)形成正确层级。
- 卡片内两行文字收紧行距：13px 级字体行盒上下各含约 3px 内边距，两行叠放视觉间隙偏大；行高 0.9 + 间距 1px 后视觉间隙约 3px，且文本块垂直居中。

## [2026-08-21] 修复启动时窗口按上次"满屏几何"恢复的问题

### 修复 — GUI
- 症状：窗口几何持久化会将最大化/近满屏状态退出时的整屏尺寸写回普通几何，下次启动直接按整屏尺寸恢复（实测注册表残留 `windowX=-52, windowY=24, windowWidth=1707, windowHeight=1019`），表现为"默认启动宽高占满整个屏幕"；Qt 不会自动将越界几何夹回屏内。
- 根因：`MainWindow.qml` 的 `Settings` 用别名直绑 `window.x/y/width/height`，Qt 文档明确别名绑定"值一变即写回持久化"——最大化时被持久化的就是整屏几何，且没有独立记录"是否最大化"。
- 修复：改为显式 `savedX/savedY/savedWidth/savedHeight/savedMaximized` 属性，`onClosing` 时保存（最大化退出只记标记、不覆盖普通几何），`Component.onCompleted` 时恢复（合并进已有的启动策略 `onCompleted`，避免重复赋值同一属性导致 QML 加载失败）。恢复前做屏内校验：尺寸越界（残留整屏/分辨率变化/首启缺省）回落默认 1400×900，位置不在当前屏内（负坐标哨兵/多屏摘除）回落居中；键名重命名使旧键残留值一次性作废。恢复逻辑在窗口映射前执行，无可见跳动。

## [2026-08-21] 修复任务栏点击无法最小化无边框窗口

### 修复 — GUI
- 前台运行时点击任务栏按钮可正常最小化/恢复。根因：Qt 把 `FramelessWindowHint` 窗口创建为纯弹出式窗口（`GWL_STYLE` 实测仅 `WS_POPUP`，无 `WS_CAPTION`/`WS_SYSMENU`/`WS_MINIMIZEBOX`），Shell 依据这些样式位决定是否投递任务栏点击最小化等交互，因此消息从未到达窗口，拦截 `WM_SYSCOMMAND` 无效。
- 采用 Electron/VSCode 同款方案：`WinFrameResizeFilter` 重构为 `WinFrameBehaviorFilter`，新增 `fixupFramelessWindowStyle()` 在窗口创建后补回 `WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX`（清除 `WS_POPUP`）；配合拦截 `WM_NCCALCSIZE` 零化非客户区（最大化时客户区收敛到显示器工作区，避免外溢边框裁切顶部内容），视觉保持无边框。补样式后最小化/恢复/系统菜单均由 `DefWindowProc` 原生处理，不再拦截 `WM_SYSCOMMAND`（拦截会因 Qt visibility 与原生状态不同步造成状态混乱）。仅 Windows 生效。

## [2026-08-21] 品牌标识上移标题栏，侧边栏释放顶部空间

### 变更 — GUI
- 标题栏高度 32px → 40px，左侧弱化小字展示「墨染 · AI 小说创作助手」作窗口定位；拖拽区更充裕。
- 移除侧边栏顶部大号「墨染」标题：避免与标题栏品牌重复，同时释放约 70px 纵向空间给项目卡片/会话列表（项目卡片补 `topMargin` 保持间距）。
- 窗口控制按钮改用 Windows 系统图标字体 `Segoe MDL2 Assets`（最小化 U+E921 / 最大化 U+E922 / 还原 U+E923 / 关闭 U+E8BB）：三枚图标同源等大，替换原文本符号「─ □ ✕」所致的大小不一；最大化按钮图标随窗口状态双向切换；按钮宽 46px（对齐 Windows 原生窗口按钮宽度）、高度占满标题栏、贴窗口右缘无间距，悬浮高亮为直角全高背景。
- 修复窗口按钮悬浮高亮上下留 6px 间隙：Qt 6.8 Material Button 样式自带 `topInset/bottomInset=6`，使 `background` 布局内缩；按钮组件显式归零 inset 后高亮铺满按钮矩形（经像素采样验证）。

## [2026-08-21] 移除内嵌 Noto Serif SC 字体

### 变更 — GUI
- 移除内嵌的 `NotoSerifSC-VF.ttf`（24MB）、CMake `font_resources` 资源注册与 `MainWindow.qml` 的 `FontLoader`。原因：开发机已安装该字体，内嵌前后渲染无差异；而 rcc C++ 生成模式不支持压缩，内嵌使仓库与包体各多 24MB，收益仅限“未装该字体的分发目标机”。标题/阅读区继续使用系统 `Noto Serif SC`（`Theme.fontDisplay` 不变）；若未来需分发到无字体环境，改用 exe 旁 `fonts/` 目录 + `QFontDatabase::addApplicationFont()` 松散部署。

## [2026-08-20] GUI 三轮：无边框窗口边缘缩放 / 内嵌 Noto Serif SC / 清理过时原型

### 增强 — GUI
- **无边框窗口边缘拖拽缩放**：新增 `WinFrameResizeFilter` 原生事件过滤器（拦截 `WM_NCHITTEST`，窗口边缘/角部 8px 命中区返回系统命中值，最大化时跳过），补回 `FramelessWindowHint` 后缺失的边缘缩放能力；仅 Windows 生效，其他平台空实现。
- **内嵌 Noto Serif SC 可变字体**（`src/novelagent_qt/fonts/NotoSerifSC-VF.ttf`，OFL 授权）：启动经 `FontLoader` 加载，family 名与 `Theme.fontDisplay` 对齐；Windows 默认不附带该字体，标题/阅读区不再回退宋体。rcc 的 C++ 生成模式不支持压缩，字体原样 24MB 嵌入（包体换字体一致性）。
- 删除根目录与现实现不符的过时浅色原型 `preview.html`。
- 补设 `organizationName`：修复 QML `Settings`（窗口位置持久化）因缺组织标识初始化失败、位置从未落盘的问题。
- 二轮细节一致性：思考过程 💭 改纯文本（该码点无法单色呈现）；章节选择弹窗补进出场动画；侧栏底部工具栏左侧空位展示版本号。

## [2026-08-20] QML 前端视觉细节优化（主题化控件 / 对话框溢出 / 深色遮罩）

### 增强 — GUI
- 新增四个主题化控件组件 `ThemedButton`/`ThemedField`/`ThemedCombo`/`ThemedSwitch`（扁平圆角、无 Material 抬升阴影），设置对话框与首启向导全量换肤；消除 API Key 输入框「顶部标签 + Material 浮动标签」双标签、胶囊按钮与暗色主题的风格冲突。
- 修复设置对话框「模型」页内容超出固定高度、导致「保存/保存并启用」按钮绘制到面板外的问题（换用紧凑主题控件后内容回落至 540px 内）；选项卡改为左对齐紧凑下划线；新增右上角关闭按钮；对话框/向导/技能弹窗统一淡入淡出进出场动画。
- 模态遮罩由 Material 默认浅色改为深色半透明（`Overlay.modal: #99000000`），与暖墨暗色主题一致。
- 侧栏设置齿轮与工具卡片运行图标 `⚙` 追加 `\uFE0E` 强制单色文本呈现，修复 Windows 下渲染为彩色 Emoji。
- 细节打磨：会话列表空状态引导文案；当前项目卡片可点击跳转项目设置；阅读区空状态垂直居中、无内容时隐藏底部字数条、无章节时收起下拉箭头；输入框 placeholder 聚焦不再消失；禁用态发送按钮改透明弱化（可用时朱砂点亮）；建议卡 hover 箭头右移微动画；SplitView 分隔线扩命中区并 hover 高亮；标题栏左侧展示窗口标题；状态栏上下文进度轨道改用更可见的分割线色。

## [2026-08-20] 修复会话管理两个严重 Bug（连带删除 / 空池新建卡死）

### 修复 — 会话管理
- **空池新建会话卡死（严重）**：`Agent::deferredToolsStub()` 原经 `currentSession()` 获取存根，而 `currentSession()` 在池空时会自动创建会话；`SessionRuntime` 构造期间回调 `system_prompt_provider`（`buildSystemPrompt` 拼接延迟工具存根）→ 触发自动创建 → 再次构造 → 无限递归。删除全部会话后点"新建"（或发消息）即卡死。修复：新增 `SessionPool::deferredToolsStub()` 无副作用获取（当前会话 → 任一会话 → 空串，绝不自动创建），`Agent` 转发之；池空时首个新会话暂缺延迟工具存根（仅影响工具提示，不影响核心工具与 tool_search）。
- **启动"双会话"与连带删除**：启动装配时 `buildSystemPrompt` 自动创建未落盘会话 A；`QmlBridge::sessionList()` 又因 `pendingNewSession()==true` 插入空 id 的"新会话"占位，同一会话显示两项；删除占位会经 `discardPendingNewSession` 把真实会话 A 一并 erase（删一个丢两个）。修复：删除占位插入，未落盘会话统一以真实 id 展示，删除走池删除只删自身；`deleteSession("")` 分支保留为防御。
- 回归测试 `test_empty_pool_new_session_no_recursion`（test_agent）：空池 + provider 依赖 `deferredToolsStub` 时 `newSession`/`createSession` 均不递归、池状态正确；全量回归 32/32 通过。

## [2026-08-20] 章节块精简头注入，提升向量检索质量

### 增强 — 检索
- `NovelChunker::chunkChapter` 新增可选 `ChapterContext`（角色/设定 id→名字 字典）：切分完成后给每个章节块 prepend 精简头（`第{order}章 {title}` + 人物 ≤6 + 地点 + 时间，单值行超 20 字按码点截断），嵌入文本与检索展示文本统一（`metadata["text"]` 同步），header 不计入块大小预算；全空章节不注入，行为与旧版一致。
- `ProjectIndexService` 索引时从项目实体构造 id→名字 字典传入；新增 `context_version` 指纹（kv_store，当前 "v1"），块头格式升级时强制整库重嵌，规避正文 hash 不变导致增量跳过的失效问题（旧库首迁自动触发一次全量重嵌，wipe 时与模型指纹对称清空）。
- 新增 `scripts/eval_retrieval.py`：零人工标注的检索质量评测（自动抽样章节并构造"标题/概要/人物"三类查询，Recall@5 + MRR@5，支持新旧库 baseline 对比），复用库中现成向量、仅查询侧调 embedding API，纯标准库。

## [2026-08-20] 实体嵌入文本拼接下沉到模型层（toEmbeddingText）

### 重构 — 模型与检索解耦
- `Character`/`Setting`/`WorldRule` 各新增 `toEmbeddingText()` 内联成员（模型头文件内实现），承接原 `NovelChunker::chunkCharacter/chunkSetting/chunkWorldRule` 的字段拼接逻辑，输出文本与之前逐字节一致。
- `NovelChunker` 三个静态方法退化为一行委托，头文件注释不再手写字段清单（原 chunkWorldRule 注释缺 `known_by` 的陈旧问题一并消除）：以后调整模型字段只改模型自身，检索层无需感知。
- 字段清单以注释形式驻留各模型头文件（含"哪些字段有意不进嵌入"的说明，如 `Setting::notes`、`WorldRule::precedence/contradicts_with`）。

## [2026-08-20] NovelChunker 内部改为 UTF-32 码点处理中文

### 重构 — 检索分块
- `chunkByParagraphs`/`splitParagraphs`/`splitSentences`/`overlapFromPrevious` 内部统一转换为 `std::u32string`（UTF-32 码点）后处理：下标/切分按"字"进行，切点天然落在字符边界，删除全部 UTF-8 续字节回退逻辑（硬切、重叠截断、句末标点字节序列匹配）。输入输出仍为 UTF-8，块尺寸仍按 UTF-8 字节口径记账，行为与既有测试完全一致。
- 新增匿名域工具：`utf8ToU32`/`u32ToUtf8` 双向转换（非法字节以 U+FFFD 容错）、`utf8ByteLength`/`utf8ByteCount` 字节折算、码点级句末标点判断 `isEndPunct`（。！？….!?）、`lastSentenceEnd`、`tailByBytes`。
- 句末标点表由 UTF-8 字节序列向量改为 `char32_t` 常量，删除 `<regex>` 依赖（段落切分改手写扫描，语义与旧正则 `\n(?:\s*\n)*` 等价）。
- 修复旧实现极端路径隐患：`max` 小于单个字符字节数时旧代码硬切会切在多字节字符中间（破坏 UTF-8），新实现整体保留该字符。
- 默认切块尺寸由 500-2000 字节调整为 600-1800 字节（≈200-600 字，按"期望字数 × 3"换算；中文 1 字 3 字节），`configure` 默认值与成员初值、注释同步。
- UTF-8 ⇄ UTF-32 转换与字节折算工具迁入 `src/utils/Utf8Utils.h`（`utils::utf8`，header-only 内联），NovelChunker 侧只保留句子标点/重叠截断等切分域逻辑。
- `llm/TokenCounter` 的本地 UTF-8 解码器删除，统一改用 `utils::utf8::decodeUtf8CodePoint`（`utf8ToU32` 亦基于它实现，两套入口行为一致）；解码新增 overlong/代理区/越界码点校验，非法输入一律返回 U+FFFD。
- 编码转换整体切换到 **simdutf v9.1.0**（vendor 进 `third_party/simdutf/`，单文件 amalgamation，新增 `novelagent_simdutf` 库目标并接入 core/core_prelinked；AVX2/FMA/BMI2 内核在支持 CPU 上自动启用）。`utils::utf8::utf8ToU32/u32ToUtf8` 改为 simdutf 校验 + SIMD 转换（合法输入路径），非法输入仅走标量 U+FFFD 兜底；`TokenCounter` 改为整串转换后统计，删除手写流式解码器 `decodeUtf8CodePoint`。
- 全量回归基线（本机）：`./scripts/verify.sh` 重建 118 个编译/链接步骤 + 32 项 ctest 全绿，总耗时约 47s（构建约 25s + 测试 21.5s）。

## [2026-08-19] NovelChunker 纯文本分块改造：删除 markdown 场景切分

### 重构 — 检索分块
- `chunkByScenes` 与其 `## Scene` 正则彻底删除：小说正文是纯文本大段文字（.txt），不存在 markdown 场景标记，不再假设正文含 markdown 结构。
- `chunkChapter` 统一走纯文本段落切分；`chunkByParagraphs` 新增超长段落兜底：单段超过上限（如整章无空行）时按中英文句末标点切成句子再聚合，修复"整章一段产出巨型 chunk"的问题；无句末标点的极端超长段按 UTF-8 字符边界硬切兜底（A11 思路）。
- 新增私有 `splitSentences`（句末标点：。！？….!?，切点位于标点之后）；聚合与重叠逻辑不变；`configure` 对 max_chunk_size 增加下限钳制。
- 新增测试：无空行整章一段、markdown 场景标记不再特殊切分、全部切点位于段落/句子边界、无标点超长段硬切（tests/ 本地保留）。
- `chunkChapter` 参数 `markdown_content` 更名为 `content`，注释更正为纯文本语义（正文非 markdown）。
- 复审修正：`configure` 增加 min 上限钳制（min>max 时封块条件永不满足、块无限增长）；`overlapFromPrevious` 改用 UTF-8 字节序列匹配句末标点（原 `find_last_of` 单字节匹配会把多字节字符内部续字节误判为标点，重叠从字符中间截断产生坏 UTF-8）；段落切分明确为"单个换行即段落边界（一行一段），连续换行/空白行合并"并补齐对应回归测试；尺寸单位注释统一为字节。

## [2026-08-17] 存储层清理：移除 vec_chunks 的 embedding_json 冗余列

### 清理 — 存储瘦身
- vec_chunks 表与全部写入路径移除 embedding_json JSON 副本（约为向量本体的 2.5 倍冗余），`get()` 不再返回原始向量（仅 id/metadata，vec0 内部存储格式不做依赖假设）；向量表体积下降约 70%。

## [2026-08-17] 存储层迁入 SQLite 单库（Phase 1：会话/向量/索引清单）

### 重构 — SQLite 集成
- 新增 `third_party/`：sqlite3 amalgamation（3.46.1，开 FTS5）+ sqlite-vec v0.1.6 + SQLiteCpp 3.3.3，全部 vendor 随仓库构建。
- 新增 `src/storage/SqliteStore`：`<项目>/.novelagent/novel.db` 唯一入口（建表迁移/WAL/事务/损坏自愈/全库互斥锁）。
- `VectorStore`（JSON 暴力扫描）删除 → `SqliteVectorStore`（sqlite-vec vec0），`IVectorStore` 接口不变。
- `IndexManifest` 删除 → 清单表 `index_sources/index_chunks/kv_store`，`ProjectIndexService` 直连 SQL、`indexAll` 单事务提交。
- `SessionPersistence` 公开接口不变，内部改 `sessions/messages/message_history` 表；删除会话置 `archived=1`（原 archive/ 归档语义）。
- 旧文件布局（vectors.json/index_manifest.json/sessions/archive）启动时清理，不做数据迁移（未发布）。
- NovelAgentApp 装配整体切换：向量库/持久化/索引服务共用 novel.db 单连接；`test_retrieval.cpp` 用例全部迁到 SqliteVectorStore。

## [2026-08-17] 重组 agent 模块目录：会话组件归入 session/，长期记忆改 longterm/

### 重构 — 目录语义对齐
- `src/agent/core/SessionPool.{h,cpp}`、`SessionRuntime.{h,cpp}` 移入 `src/agent/session/`
  （与会话持久化 SessionPersistence 同目录，core/ 只保留 Agent 门面 + AgentState/CoreLoop/ExecutionTracer 执行引擎）。
- `src/agent/memory/` 更名为 `src/agent/longterm/`（仅含长期记忆存储 LongTermMemoryStore，原名易与 context/Memory.h 混淆）。
- 受影响引用全部更新：`core/Agent.h/.cpp`、`session/SessionPool.h/.cpp`、`session/SessionRuntime.cpp`、
  `NovelAgentApp.h`、`tools/SaveMemoryTool.cpp`、`index/ProjectIndexService.cpp`、
  `tests/test_context_manager.cpp`、`tests/test_index_manifest.cpp`、`tests/test_agent.cpp`（补直接 include）、
  `cmake/Sources.cmake` 目录段同步调整。
- 文件移动使用 `git mv`，无内容变更，历史可追溯。

## [2026-08-17] 移除 SIGINT（Ctrl+C）取消机制 — 暂停改由前端按钮负责

### 清理 — 冗余取消路径
- 背景：多会话并行 + GUI 化后，暂停已由前端按钮触发（`AgentPanel.qml → QmlBridge::cancelRequest()
  → Agent::requestCancel() → 按会话置 cancel_requested_`）。SIGINT 仅是同一标志的另一条置位路径
  （`g_cancel_flag` 指向当前会话的 cancelFlag），且 GUI 模式下手动 SIGINT 语义模糊，故整体移除。
- `src/Bootstrap.h`：**删除**（内容仅为 SIGINT handler + g_cancel_flag + installSigint）。
- `src/main_gui.cpp`：移除 include 与 `installSigint()` 调用。
- `src/novelagent_qt/QmlBridge.cpp`：移除 include 与 3 处 `g_cancel_flag` 管理（析构/rebuildApp）。
- 死代码清理：`Agent::cancelFlag()`、`SessionRuntime::cancelFlag()`（仅为 SIGINT 接线存在的访问器）。
- 效果：Ctrl+C 恢复进程默认行为；优雅退出/重建路径不变（窗口关闭 → QmlBridge 析构 → shutdown）。

## [2026-08-17] 清理过时/失效文档（15 个）

### 清理 — 一次性方案、评审快照与过期状态文档
- **已实施的计划/方案**：`design/QT_INTEGRATION_PLAN.md`（Qt 迁移已完成，且"保留 CLI"与现状不符）、
  `superpowers/plans/2026-07-26-moran-ui-redesign.md` + `specs/2026-07-26-moran-ui-redesign-design.md`
  （墨染 UI 改版已落地）、`superpowers/plans/2026-07-30-yaml-cpp-integration.md`（yaml-cpp 已集成）、
  `superpowers/plans/2026-07-30-global-rules-layer.md`（全局/项目双层规则层已落地）。
- **状态过时的设计文档**：`design/PROGRESSIVE_TOOL_LOADING.md`（标注"未实现"，实际 ProgressiveToolProvider
  已实现并接入）、`design/CONTEXTUAL_TOOL_PROVIDER_STATUS.md`（组件从未接入，已被渐进式方案取代）。
- **未采纳/被取代的方案**：`plan/QUEUE_QT_INTEGRATION_PLAN_2026-07-20.md`（队列方案未实施，被多会话并行架构
  取代）、`plan/TOOL_CALL_IMPROVEMENT_PLAN_2026-07-20.md`（混合状态无更新，引用旧架构）。
- **结案的评审快照**：`review/CONCURRENT_INPUT_HANDLING_2026-07-19.md`（方案 A 未采纳，实际走多会话并行）、
  `review/PIN_STALE_DATA_REVIEW_2026-07-19.md`（"未修复"结论过时，kMaxAutoPinned=12 已解决）、
  `review/QUANTCLAW_REFERENCE_REVIEW_2026-07-20.md`、`review/SIGINT_HANDLING_REVIEW_2026-07-27.md`（结论"无需修复"）、
  `review/MULTI_SESSION_PARALLEL_REVIEW_2026-08-03.md` + `MULTI_SESSION_PARALLEL_REFERENCE_2026-08-04.md`（蓝图已落地）。
- **保留**：`DEV_GUIDE.md`、`design/CONTEXT_OVERFLOW_RECOVERY.md`（实现记录仍有效）、`review/DESIGN_REVIEW.md`、
  `review/REVIEW_STATUS.md`（已更新过时章节）、`design/TOOL_CHAIN_DESIGN.md`（chain 工具设计提案）。
- `REVIEW_STATUS.md`：§〇 多会话评审标记为已实施；过时引用（已删文档/待定状态）全部修正为结案说明。

## [2026-08-05] 修复 createPoolSession 未同步池焦点（取消按钮失效回归）

### 修复 — 取消按钮失效
- **回归根因**：Minor #7 将 `cancelRequest` 改为按池当前焦点（`agent.currentSessionId()`）定位会话后，
  `createPoolSession` 只更新 QmlBridge 的 `current_session_id_`、未同步池焦点（`SessionPool::createSession`
  不设当前）；新建会话后发送消息、会话在跑时点取消，`currentSessionId()` 取到旧/幽灵会话 → 取消静默失效。
- **修复**：`createPoolSession` 末尾 `switchSession(id)` 同步池当前焦点。
- 全量回归 30/30 通过。

## [2026-08-05] 清理多会话审查遗留 Minor（#4/#5/#6/#7）

### 清理 — 次要问题
- **#5**：`ThreadPool` 移除 `num_threads==0→12` 兜底分支（D9 要求；调用方均传正数 4）；测试
  `test_zero_threads_fallback` 改为 `test_zero_threads_no_fallback`（断言 0 不再回退）。
- **#4**：`QmlBridge::newSession()` 同步焦点到新会话（`Agent::currentSessionId()` 新增转发 +
  current_session_id_/recent_sessions_ 更新 + emit currentSessionIdChanged），避免后续 sendMessage 发往旧会话。
- **#7**：`QmlBridge::cancelRequest()` 改为以池当前会话为准（`agent.currentSessionId()`），消除与
  current_session_id_ 失同步时误取消它会话的风险。
- **#6**：`SessionPool::deleteSessionRuntime` 超时分支加 `wait_for(0)` 防御性检查——任务恰在超时边界完成
  则直接移除，避免回滚一个已按 cancelled 结束的会话。
- 全量回归 30/30 通过。

## [2026-08-05] 修复并发上限 TOCTOU（P9 检查+占用原子化）

### 修复 — 并发上限计数正确性
- **根因**：`canSubmit()` 检查（持锁即释放）与 `in_flight_` 插入占位是两步，中间不持锁；多线程并发提交时
  两个线程可同时通过检查，突破 kMaxConcurrent=4。
- **修复**：`process`/`submitProcess` 的「并发检查 + 池提交 + in_flight_ 占位」合并进持锁 `in_flight_mutex_`
  一次性完成；删除不再使用的 `canSubmit()`。
- 持锁提交不引入死锁：ThreadPool::submit 仅入队（O(1)，不阻塞），任务推进由 workerLoop 在锁外执行。
- 测试：`test_agent` 新增 `test_concurrent_submit_cap`（8 线程并发提交，断言 accepted ≤ 4；并发竞态窗口小，
  难稳定复现 RED，作为回归保护）。
- 全量回归 30/30 通过。

## [2026-08-05] 修复全局 busy 并发账目错误（D12 聚合信号）

### 修复 — 全局 busy 语义修正
- **根因**：`QmlBridge::busy_` 是单布尔，`runAgent` 置 true、**每次** `on_complete` 置 false。两会话并发时
  任一会话完成即清 false，另一会话仍在跑时 `rebuildApp`/`rebuildIndex`/`setSkillEnabled` 会放行重建 app_，
  叠加析构 UAF 风险。
- **修复**：`busy_` 移除，`busy()` 改为聚合信号 = `indexing_`（索引重建中）或 `app_->agent().anyRunning()`
  （任一会话运行）。
- **新增**：`SessionPool::anyRunning() const`（遍历 pool_ 查任一 `running()`）+ `Agent::anyRunning()` 转发。
- **rebuildIndex 独立互斥**：改用 `indexing_` 原子标志（原复用全局 busy 做自身互斥，聚合后不再适用）。
- **newSession 移除守卫**：多会话下新建会话仅建独立 runtime，不影响在跑会话，去掉全局 busy 阻塞。
- `runAgent`/`on_complete` 不再手动 store busy，仅 emit `busyChanged` 通知 QML 重新求值 `busy()`。
- 测试：`test_agent` 新增 `test_any_running`（全空闲 false → 慢任务运行中 true → 完成后复位 false）。
- 全量回归 30/30 通过。

## [2026-08-05] 修复 QmlBridge 析构时后台回调 use-after-free（方案 A+B）

### 修复 — 多会话退出/重建生命周期安全
- **方案 A**：`SessionPool` 新增 `cancelAllAndWait(timeout=2s)`（持锁收集全部 in-flight future +
  逐个 `requestCancel` → 释放锁 → 依次 `wait_for` 等待退场，任一超时即停止等待避免 N×timeout）；
  `Agent` 新增 `shutdown()` 转发；`~QmlBridge` 析构体先 `app_->agent().shutdown()` 再 `joinWorker()`，
  确保后台池线程的 on_complete 在对象与 app_ 存活时执行完毕。
- **方案 B**：`QmlBridge` 新增生命周期令牌 `alive_`（`shared_ptr<atomic<bool>>`），`on_complete` 捕获其
  weak_ptr 并先 `lock()` 检查，析构时复位 token——兜底 A 的 2s 超时窗口，彻底避免残留任务访问已析构的 this。
- `on_complete` 访问 `app_` 前加 `app_ &&` 判空。
- 测试：`test_agent` 新增 `test_shutdown_waits_inflight`（2 并发慢任务 + shutdown，断言 on_complete 全部执行
  且等待时间远小于串行等满超时）。
- 全量回归 30/30 通过。

## [2026-08-05] 清理审查遗留 Minor（B2/A3/D9/P9/跨线程竞争）

### 清理 — 补齐参考文档的次要项
- **B2**：`SessionPersistence::listSessions` 移除 `updated_at` 排序（最近使用顺序由前端
  `recent_sessions_` 维护，后端返回存储顺序）。
- **D9**：`ToolPipeline` 去掉 `num_threads=0` 全串行分支（pool_ 恒创建，调用方传正数如每会话 2），
  `execute` 移除 `!pool_` 判断（保留单工具调用走串行的优化）。
- **跨线程竞争**：`SessionRuntime::running_`/`persisted_` 改 `std::atomic<bool>`（池线程写、GUI 线程读）；
  `usage_` 加 `mutable std::mutex usage_mutex_` 保护（`refreshUsage` 写 / `contextUsage` 读）。
- **A3**：`SessionManager` 重命名为 `SessionPool`（类名 + 文件 + Agent 成员 `session_pool_` +
  CMake + 注释），纯机械重命名，功能不变（Agent 作为薄门面持有会话池）。
- **P9 测试**：`test_agent` 新增 `test_concurrency_full`（提交 4 个并发慢 process 占满 in-flight，
  第 5 个同步 process 返回 `concurrency_full`）。
- **D10**：取消占位消息采用"过滤"方案（`conversationHistory` 跳过 `is_control`，D10 允许的选项之一），
  无需特殊渲染。
- 全量回归 30/30 通过。

## [2026-08-05] 接入 system_prompt_provider（E1/D11 定版落地）：会话创建时重建 prompt

### 修复 — provider 注册了但从未被调用，save_skill 新增技能下个会话不可见
- `SessionManager::makeRuntime` 注入共享的 `system_prompt_provider_`（此前硬编码 `nullptr`）。
- `SessionRuntime` 构造时经 provider 重建 system prompt（E1：会话创建/物化时读到最新技能目录，
  save_skill 下个会话对 LLM 可见）；provider 为空或抛异常时回退构造注入的 `system_prompt` 兜底；
  会话中途不重建（保 KV cache）。
- 测试：`test_agent` 新增 `test_system_prompt_provider_rebuild`（provider 非空重建、为空回退、
  抛异常回退不崩溃）；全量回归 30/30 通过。

## [2026-08-05] 删除运行中会话：独立删除标志（修 cancel 被吞）+ 2s 超时不强制移除

### 修复 — 删除排队任务时取消被吞、删除运行中会话无限等待
- **问题 B（cancel 被吞）**：`SessionRuntime` 新增独立 `delete_requested_` 原子标志与
  `setDeleteRequested/clearDeleteRequested/deleteRequested`；`deleteSessionRuntime` 删除运行中会话时
  置位，`process()` 开头在 `resetCancel()` 之后检查——排队任务启动时立即以 `cancelled` 退出，
  不被 `resetCancel()` 清零覆盖（此前删除者会白等一整轮 LLM）。
- **问题 A（无超时）**：`deleteSessionRuntime` 的 `fut.wait()` 改为 `wait_for(2s)`；超时后**不强制移除**
  （安全优先——池线程的异步任务捕获 runtime 裸指针，此刻 `pool_.erase` 析构会导致 use-after-free），
  恢复会话标志（清除取消/删除请求，稍后可重试删除）并返回 false。`QmlBridge::deletePoolSession`
  对删除失败短路，不执行归档（会话未删成）。
- 全量回归 30/30 通过。

## [2026-08-05] 删除池会话补持久层归档（D4 定版落地）：已删会话重启不再复活

### 修复 — deletePoolSession 只移除内存池，漏掉持久层归档
- `QmlBridge::deletePoolSession` 在 `deleteSessionRuntime`（cancel+wait+移除内存池）成功后，补调
  `persistence->deleteSession(id)`：快照 `<id>.json` + 完整历史 `<id>.history` 归档到 `archive/`
  并从 index 移除；未打开项目（无持久化）时跳过。此前池会话删除只 `pool_.erase`，磁盘文件残留，
  重启后从持久层复活（历史会话删除已在上轮修复，本修复补齐池会话路径）。
- 顺序：先等运行中会话退出移除内存池，再归档持久层（避免运行时读到写到一半的文件）。
- 全量回归 30/30 通过。

## [2026-08-05] 按会话 busy（D12/阶段 4 定版落地）：后台会话继续跑，可切到空闲会话发消息

### 修复 — GUI 输入区从全局 busy 改为按"当前查看会话"判断
- **新增 `sessionBusy` 属性/信号**（QmlBridge）：返回当前查看会话是否在跑
  （`session(currentSessionId)->running()`）；在 runAgent 开始/完成、switchPoolSession 时发射
  `sessionBusyChanged`。
- **AgentPanel 输入区改用 `bridge.sessionBusy`**：`sendCurrentMessage` 与发送按钮的
  禁用/文字/颜色（"发送"↔"取消"）都按当前查看会话运行态判断——会话 A 后台跑时切到空闲会话 B，
  B 输入框可用、按钮显示"发送"，可发新对话（D12 核心场景可达）。
- **取消语义修正**：`cancelRequest` 仅在当前查看会话 `running()` 时取消（不再受全局 busy 误导，
  空闲会话不会显示"取消"按钮）。
- **状态栏忙碌动画**按当前查看会话（E9 精神）。
- 保留全局 `busy_` 用于重建索引/技能管理等全局互斥操作（非发送判断），语义不变。
- 全量回归 30/30 通过。

## [2026-08-05] 历史会话懒物化（P8 定版落地）：点开才物化、启动不物化、历史可打开/可删

### 修复 — 会话恢复功能断裂：历史会话既看不到、打不开、也删不掉
- **启动不再物化**：`AppAssembly::setupPersistenceAndVectorStore` 去掉 `loadSessionState()`（此前经
  `currentSession()` 自动建 `s-multi-1` 空会话并建 client，白占并发槽位且遮蔽历史列表）；启动仅
  `setPersistence` 注入持久化层。
- **新增物化入口 `materializeSession(id)`**（SessionManager/Agent）：已在池则仅切当前焦点；不在池且
  持久层存在该 id 时建 runtime + `loadSessionState()` 恢复历史消息并设为当前焦点；持久层不存在返回
  false（不建幽灵 runtime）。QmlBridge `switchPoolSession` 对历史 id 走物化。
- **列表合并**：`sessionList()` 返回"池会话 + 持久层历史会话"合并列表，去掉"有池会话就遮蔽持久层"
  的分支；历史会话可点开物化，持久层异常仍 try/catch 不穿透 QML。
- **历史会话可删**：`deleteSession` 对不在池的历史 id 改走 `persistence->deleteSession`（快照 + 完整
  历史归档到 archive/），不再静默失败。
- **GUI 属性读取不触发物化（P8）**：`QmlBridge::modelName`/`activeProviderName` 改从 provider 配置读
  （`NovelAgentApp::modelName`/`providerName` 取自 `client_.config()`），不再经 `agent().client()` 触发
  `currentSession()` 自动建会话。
- 测试：`test_agent` 新增 `test_materialize_history_session`（启动不物化、materializeSession 恢复历史
  2 条、不存在 id 拒绝且不影响既有会话）；全量回归 30/30 通过。

## [2026-08-05] 项目锁下沉 ProjectAccess 访问层 + 撤销 ToolPipeline 批次锁（P2/P3/C1 定版落地）

### 重构 — 锁集中在 ProjectAccess，Project 恢复纯数据模型，工具/GUI/索引统一经访问层
- **Project 恢复纯 POJO（D7/C1/P3）**：移除 Project 上的锁与事务方法，恢复为纯数据模型
  （序列化/ProjectIO/测试直接字段访问零改动）；锁、事务方法、withLock、快照读全部集中到
  `ProjectAccess`（`src/project/ProjectAccess.h/.cpp`，持有 `shared_mutex`）。
- **撤销批次锁（P2 定版）**：删除 `ToolPipeline::setProjectLock`/`project_lock_` 与 `execute()` 入口的
  整批持锁（含 LLM/文件 IO 全在锁内）；`SessionManager`/`SessionRuntimeDeps` 同步移除 project_lock
  注入链。并发安全完全由 ProjectAccess 方法级锁承担。
- **ProjectAccess 受控访问层**：`withReadLock`/`withWriteLock`（lambda 接收 `Project&`，锁已持有）、
  事务方法 `addXxx`/`updateXxx(id, fn)`/`removeXxx`（独占锁内一次读-改-写，自动 markDirty）、
  快照读 `getXxx`（共享锁内拷贝）、`path()`/`title()` 只读 getter、`save()`（锁内快照 + 脏标记
  test-and-clear，文件 IO 锁外，含防漏 markDirty 全量守卫）；删除 Phase 3.5 遗留死代码 `IProjectAccess.h`。
- **工具全量改造（198 处访问）**：`ToolDependencies.project` → `project_access`，`REGISTER_TOOL` 宏/
  全部工具构造函数改持 `shared_ptr<ProjectAccess>`；读工具走快照/withReadLock（selector 类锁内查询），
  写工具"锁外读快照计算 → 锁内小改 → 锁外 save"；跨聚合删除（delete_chapter/delete_character/
  delete_setting/delete_world_rule）用 withWriteLock 一次级联完成。
- **P4 显式只读标记**：18 个只读工具类覆写 `isReadOnly() = true`；`ToolRegistry::isReadOnly` 移除前缀
  启发式兜底（默认非只读，保守），`IToolProvider::defaultIsReadOnly` 删除。
- **ProjectIO**：抽出 `saveSnapshot(Project&, flags)`（纯序列化 + 按脏位写盘），`save()` 复用。
- **索引服务（E8）**：`ProjectIndexService` 改持 `ProjectAccess`，`indexAll` 内部加 `std::mutex` 串行化，
  聚合数据 withReadLock 快照拷贝后锁外切分/嵌入。
- **GUI/装配**：`QmlBridge` 的 projectName/projectPath/chapterList/loadChapter/rebuildIndex 与
  `NovelAgentApp`/`AppAssembly` 的 path/title 读取统一改走 `projectAccess()`。
- **顺带修复**：delete_setting/delete_character/delete_world_rule 级联修改遗漏脏位标记（settings/
  outline/world_rules 落盘丢失级联清理）——已补全脏位。
- 测试：新增 `tests/test_project_lock.cpp`（6 用例：快照拷贝/事务方法/withLock/并发写无丢失/
  读写混合不崩溃/save 落盘重载）；工具测试构造适配 `make_shared<ProjectAccess>(tp.project)`；
  全量回归 30/30 通过。

## [2026-08-04] 并发拒绝（P9）+ 构造注入（P5）

### 实现 — P9 并发上限拒绝 + P5 SessionRuntime 核心依赖构造注入
- **P9**：`SessionManager::canSubmit()` 按 in-flight（已提交未完成）数量与 `kMaxConcurrent=4`
  对齐线程池上限；`process`/`submitProcess` 满额时返回/回调 `finish_reason="concurrency_full"`，
  不排队；QmlBridge 完成回调显示"并发已满，请稍后再试"。
- **P5**：新增 `SessionRuntimeDeps`（persistence/calibrator/project_lock/exec_config/config/
  system_prompt/model_limit），`SessionRuntime` 构造函数一次性注入，**删除 7 个 setter**；
  `makeRuntime` 改为 deps 构造。
- **对齐 D11**：`SessionManager` 的 setXxx 仅更新共享源、删除广播循环（配置仅创建时生效；
  会话懒创建，启动装配先于任何会话）。
- 测试：`test_multi_session_persistence` 改为 deps 构造 runtime。
- 全量回归 29/29 通过；GUI 启动正常。

## [2026-08-04] process 异步提交（P1）

### 实现 — 异步 process + 完成回调，GUI 不再用专用 worker 线程
- `SessionManager::submitProcess(session_id, input, cb, on_complete)`：提交共享线程池立即返回，
  池线程执行 process 后调用 `on_complete(session_id, response)` 并清理 in-flight；同步
  `process(session_id, ...)` 保留供测试/API。
- 修复 `deleteSessionRuntime` 潜在死锁：不再持有 `in_flight_mutex_` 时 wait（异步任务的
  清理逻辑也需要该锁），改为拷贝 shared_future 后释放锁再 wait。
- `Agent::submitProcess` 转发；`QmlBridge::runAgent` 改异步提交（去掉每消息专用 worker 线程），
  完成回调经 QueuedConnection 投递 GUI 线程发射信号；`runIndexUpdate` 移到完成回调。
- `sendMessageToSession`/`sendMessage` 改为按**目标会话自身运行态**检查（支持多会话并行提交），
  不再被全局 busy_ 拦截。
- 全量回归 29/29 通过；GUI 启动正常。

## [2026-08-04] 持久化去 active 字段（D3）+ 修幽灵会话

### 重构 — SessionPersistence 按 id 隔离，移除单一 active 字段
- 删除 `activeSessionId()`/`switchSession()`/legacy `save(memory)`/`load()`；`index.json` 改为
  `{sessions:[...]}`（无 active）；`indexValid`/`rebuildIndexFromDisk`/`createSession`/`deleteSession`
  全部去除 active 处理。
- **修幽灵会话**：`rebuildIndexFromDisk` 在磁盘无会话文件时不再自动创建空会话（此前 index 首次
  生成会多出一个 `s-<ts>` 幽灵会话）。
- `QmlBridge`：sessionList 持久层回退路径不再依赖 active（均不标 active）；deleteSession 改为按
  "当前查看会话"判断重建聊天流。
- 测试适配：7 个 SessionPersistence 测试改为 session_id API（`save("s-test", ...)`/`load("s-test")`）；
  损坏索引测试场景改为"空 id 条目"触发重建。
- 全量回归 29/29 通过；GUI 启动正常。

## [2026-08-04] 去历史归档 sink（D4）

### 重构 — SessionRuntime 直接落盘完整历史，删除三层回调链
- 删除 `Agent::setHistorySink`/`SessionManager::setHistorySink`/`SessionRuntime::setHistorySink`
  与 `history_sink_` 成员；`AppAssembly` 移除 sink 注册 lambda。
- `SessionRuntime::applyCompaction` 改为直接调 `persistence_->appendHistory(session_id_, compacted)`
  （runtime 已持有 persistence，归属天然正确）。
- 测试 `test_history_sink_wiring` 改为直接断言 `<sid>.history` 落盘内容。
- 全量回归 29/29 通过。

## [2026-08-04] 多会话并行收敛重构（D2）+ 取消占位 is_control（P6）

### 重构 — Agent 收敛为门面，SessionManager 收敛为 SessionPool（消除双路径）
- `SessionManager` 重写为会话池容器：持有 `pool_`（map<session_id, SessionRuntime>）、共享线程池、
  in-flight 跟踪、跨会话项目锁、共享装配配置；`createSession/newSession/switchSession/deleteSession/
  process/pinMessage/rewindTo/saveSessionState/loadSessionState` 全部按池语义实现。
- `Agent` 移除全部单会话成员（client_/memory_/state_/progressive_tools_/pipeline_/tracer_/usage_/
  last_warnings_）与重复编排逻辑（processSerial/applyCompaction/refreshUsage/compactConversation），
  变为薄门面，公有 API 全部转发 SessionManager（E6 接口不变）；构造函数改为 `Agent(factory, registry)`。
- 消息级操作（pin/unpin/edit/rewindTo/checkpointIndices/compactConversation）移入 SessionRuntime。
- `NovelAgentApp` 移除 `memory_` 成员（会话内存由各 SessionRuntime 持有）。
- 测试适配：13 处 Agent 构造更新；B2/延迟创建/历史归档测试改为池语义断言；删除 2 个
  测旧单内存 SessionManager 语义的用例。
- 全量回归 29/29 通过；GUI 启动正常。

### 实现 — 取消占位消息 is_control 标记（P6）
- `llm::Message` 新增 `bool is_control`；`cancelledAssistant()` 置 true；SessionPersistence 序列化
  is_control；`conversationHistory()` 跳过控制消息（UI 不显示"已取消"占位为真实回复）。

## [2026-08-04] 多会话并行收尾优化（client 休眠 / index 锁 / 最近排序）

### 优化 — 补齐蓝图三项遗留项
- **client 空闲休眠释放（D6）**：`SessionRuntime::releaseClient()` 释放 HTTP 连接，`process()`
  懒重建；`Agent::releaseIdleClients()` 释放所有非运行会话的 client（运行中保留），
  `QmlBridge::switchPoolSession` 切走时触发。
- **SessionPersistence index 加锁（D3）**：新增 `index_mutex_`，`save`/`save(session_id)`/
  `createSession`/`switchSession`/`deleteSession` 的索引读-改-写串行化（会话文件按 id 隔离无需锁）。
- **池会话最近使用排序（B2/D3）**：`QmlBridge` 维护 `recent_sessions_`，创建/切换/发消息时置顶，
  `sessionList()` 按此前缀排序展示池会话。
- 全量回归 29/29 通过；GUI 启动正常。

## [2026-08-04] 多会话并行 GUI 层接入（阶段 4 核心）

### 实现 — QmlBridge/QML 多会话桥接 + 侧栏管理
- `QmlBridge` 新增 `Q_PROPERTY(QString currentSessionId)`（当前查看会话焦点）+ `createPoolSession()`/
  `switchPoolSession()`/`sendMessageToSession()`：`sendMessage` 无当前会话时自动建池会话并发往池。
- 流信号全部加 `sessionId` 参数（tokenReceived/reasoningReceived/toolCallStarted/toolCallFinished/
  responseComplete），`runAgent` 改为 `process(session_id, input, cb)` 走多会话池路径。
- `AgentPanel.qml` 各 onXxx 处理器按 `bridge.currentSessionId` 过滤，后台会话输出不污染当前视图。
- `conversationHistory()` 优先读当前池会话的独立内存（否则回退单会话 active）。
- `sessionList()` 置顶展示池会话（标题取首条 user 消息、当前查看标 active、运行态标 running）；
  `SidebarPanel.qml`「+ 新建」接 `createPoolSession()`、列表点击接 `switchPoolSession()`、
  删除按钮经 `deleteSession` 智能路由到 `deletePoolSession()`（删当前会话自动切剩余）。
- 全量回归 29/29 通过；GUI 启动正常。

## [2026-08-04] 多会话并行核心实现（阶段 0-3 部分）

### 实现 — 每会话独立运行时 + 并行执行 + 按 id 持久化 + 并发保护 + 生命周期
- 新建 `SessionRuntime`（src/agent/core/SessionRuntime.h/.cpp）：每会话独立运行时，持有
  memory/client/state/progressive_tools/pipeline/tracer/usage/cancel/session_id/persisted，
  含 process/压缩/归档逻辑（从 Agent 迁移）；构造时复用 Agent 的 process 主流程。
- `Agent` 容器化：新增 `pool_`（map<session_id, unique_ptr<SessionRuntime>>）、`createSession()`、
  `session(id)`、`process(session_id, input, cb)`、`deleteSessionRuntime(id)`、`sessionIds()`；
  `ContextUsage`/`AgentExecutionConfig` 移入 SessionRuntime.h。
- 共享线程池（`agent::ThreadPool`, 4 线程）：`process(session_id, ...)` 提交进池并行执行，
  多会话可并发跑在池工作线程上。
- `SessionPersistence` 新增 `save(session_id, memory)`/`load(session_id)` 重载（D3，按 id 隔离，
  不依赖 active）；`SessionRuntime` 每轮结束按本会话 id 落盘、可恢复。
- 并发保护：`TokenCounter` 有状态方法加内部 `std::mutex`（D8）；`ToolPipeline` 新增跨会话共享项目锁
  （D7/P2，execute 按含写工具加独占/共享锁），Agent 持有共享锁并注入各 runtime。
- `isReadOnly` 注册标记（E7/P4）：`IToolProvider` 新增 `isReadOnly` 接口（默认前缀启发式），
  `ToolRegistry` 按 `ToolEntry.is_readonly` 显式标记优先、否则启发式；`BuiltInTool` 加
  `isReadOnly()` 虚方法（默认 false）；`registerTool` 加 `is_readonly` 参数；`ToolPipeline` 改用
  `tools_.isReadOnly(name)`。
- 生命周期（阶段 5）：`Agent::process` 记录 in-flight 任务，`deleteSessionRuntime` 对运行中会话
  cancel + wait 再移除，防 use-after-free。
- 新增测试：`test_multi_session_pool`（独立运行时）、`test_parallel_sessions`（两会话并发）、
  `test_multi_session_persistence`（按 id 落盘恢复）。
- 全量回归 29/29 通过。

### 说明
- 本提交实现多会话并行后端全量（阶段 0-2、阶段 3、阶段 5 生命周期）。阶段 4 GUI 层为后续工作。

## [2026-08-04] 多会话并行架构参考文档（整合全部决策）

### 文档 — 新建整合后的实施蓝图
- 新建 `docs/review/MULTI_SESSION_PARALLEL_REFERENCE_2026-08-04.md`，整合 D1–D12 与新增
  E1–E10 决策，作为并行化改造的唯一实施依据，替代原评审文档：
  - **E1** 修正阶段 3 锁方案矛盾（锁在 Project 内部，非 ToolPipeline 入口）；
  - **E2** Agent 保留类名改造成门面+池，不新建 AgentFacade；
  - **E3** SessionManager 解散吸收进 SessionPool，Agent 直接持有 pool_；
  - **E4** 启动恢复 `last_viewed`（index 保留该字段，非 active 语义）；
  - **E5** 全局取消复位时机（cancelAll 后调用方显式复位）；
  - **E6** Agent 公有转发接口全部保留（签名不变）；
  - **E7** `isReadOnly` 改为工具注册时带 `is_readonly` 属性；
  - **E8** `runIndexUpdate` 内部加锁串行化；
  - **E9** 状态栏显示当前查看会话 usage；
  - **E10** SessionRuntime 内部 process 不带 session_id（门面定位后调 `rt.process(input,...)`）。
- 参考文档含：目标架构、最终决策清单（决策 A/B/C/D/E 五组）、并发安全矩阵、阶段 0→5 实施计划、
  资源账、风险边界。原评审文档保留作决策过程存档。

## [2026-08-04] 多会话并行架构决策定稿（D1–D12）

### 文档 — 多会话并行评审的 12 项实施决策全部确认
- 在 `docs/review/MULTI_SESSION_PARALLEL_REVIEW_2026-08-03.md` 追加「九、实现前待澄清清单」，
  逐项澄清文档内部矛盾、与已落地实现的冲突、以及未定义点，共 12 项（D1–D12）：
  - **D1** 前端把 sessionId 显式传给后端（`process(session_id, ...)`），pending 走方案 C（id 提前、文件延迟）；
  - **D2** SessionManager 收敛为 SessionPool，消息操作下放 SessionRuntime；
  - **D3** `save/load` 显式传 session_id、删 `active` 字段、`updated_at` 排序移前端维护；
  - **D4** 去全局 sink，SessionRuntime 直接落盘历史，删除前 cancel+wait；
  - **D5** pending 保留，id 生成时机改到创建时；
  - **D6** client 迁入 SessionRuntime，按需物化 + 空闲休眠释放；
  - **D7** Project 自带 shared_mutex（项目级单锁）+ 锁外计算/锁内小改；
  - **D8** 校准器有状态方法加内部 mutex，跨会话共享是收敛必要条件；
  - **D9** 每会话工具线程 = 2，去掉 num_threads=0 串行分支；
  - **D10** 删除走强制取消（B）+ wait 超时，取消占位消息加控制标记；
  - **D11** 装配仅创建时生效（方案 B）；
  - **D12** 确认全量并行（后台会话继续跑为硬需求）。
- 全部 12 项已在 9.5 确认表标记 ☑ 已确认，作为后续并行化改造的实施蓝图（阶段 0→5）。

## [2026-08-04] 压缩归档会话归属修正 + 历史层并发约束 + 归档链路测试

### 修复 — 压缩归档不再把 pending 新会话消息误记到旧会话
- `Agent::applyCompaction` 归档被压缩消息时，改用 `SessionManager::currentSessionId()`
  解析当前内存所属会话，而非动态查持久层 `activeSessionId()`；
- 新增 `SessionManager::currentSessionId()`：pending（未落盘新会话）时返回空串，压缩
  归档据此跳过，避免把新会话的被压缩消息写入旧会话的历史层；非 pending 时即 active 会话 id；
- 新增 `tests/test_context_manager.cpp` 的 `test_current_session_id` 覆盖 pending/非 pending 归属。

### 补强 — 历史层并发约束与归档链路测试
- `SessionPersistence::appendHistory` 补充线程约束注释：read-modify-write 依赖单线程
  调用方（Agent 状态机串行化 process/compactConversation），writeText 原子写仅保证崩溃
  时文件完整、不提供并发安全；
- 新增 `tests/test_agent.cpp` 的 `test_history_sink_wiring`：验证压缩后 history sink 按
  正确会话 id 收到被压缩消息、无可压缩内容时不触发归档。

## [2026-08-04] 会话删除时完整历史一并归档（双层持久化补全）

### 修复 — 删除会话不再把完整历史层孤儿化
- `SessionPersistence::deleteSession()` 删除会话时，将完整历史 `<id>.history` 一并归档到
  `archive/<id>.history`（原子写）再删除原文件，与快照层归档语义一致；此前 `.history` 文件
  在 `sessions/` 下无索引、无归档地永久残留，删除后无法从 archive 恢复。
- `makeSessionId()` 查重纳入 `<id>.history` 与 `archive/<id>.history`，避免同秒 id 复用导致
  残留历史被误追加或归档文件被覆盖。
- 头文件布局注释与删除日志同步更新为「快照与完整历史归档到 archive/」。
- 新增 `tests/test_context_manager.cpp` 的 `test_delete_archives_history` 覆盖删除归档。

## [2026-08-04] 会话延迟创建：新建不落盘，首条消息才落地

### 重构 — 消除空会话文件堆积，会话创建改为「首条消息触发」的延迟模式
- `SessionManager` 新增 `pending_new_` 状态与 `pendingNewSession()`：`newSession()` 不再立即
  `createSession`，改为保存当前会话后清空内存并标记 pending，不落盘新会话；
- `saveSessionState()` 成为延迟创建落地点：pending 且内存为空（未发消息）→ 不落盘；pending 且
  内存非空（首条消息已注入）→ 此刻才真正 `createSession` 并保存；
- `switchSession()` 处理 pending：有内容则先落地再切，空则直接丢弃（不产生空会话文件）；
- `deleteSession()` 删除旧 active 会话时丢弃 pending；新增 `discardPendingNewSession()` 供前端
  删除占位条目时放弃临时会话；
- `Agent` 转发 `pendingNewSession()`/`discardPendingNewSession()`；
- `QmlBridge::sessionList()` 在 pending 时置顶插入「新会话」占位条目（ID 留空、标 active），
  `deleteSession()` 对空 ID 走丢弃分支；
- 新增 `tests/test_context_manager.cpp` 的 `test_session_lazy_creation` 覆盖延迟创建状态机。

### 审查与补强 — 注释修正 + 测试缺口补齐
- 修正 `Agent::newSession()` 过时注释：由旧「立即创建并切换」改为「延迟创建」语义描述；
- 补齐 `test_session_lazy_creation` 两个缺口：`discardPendingNewSession()` 直接调用、
  pending 状态下内存非空时 `switchSession` 切走落地为新会话；
- 新增 `tests/test_agent.cpp` 的 `test_lazy_creation_via_process`：经 Agent.process 端到端
  验证「newSession 不落盘、首条消息才创建会话」完整链路。
- `SessionManager::newSession()` 增加 pending 复用分支边界防御：非空 pending（首条消息已注
  入但未落盘）时先落地再复用，避免静默丢用户输入；空 pending（最常见）仍直接复用。
- `QmlBridge::newSession()` 状态提示由「新会话已创建」改为「新会话已就绪（首条消息后保
  存）」，反映延迟创建语义。

## [2026-08-04] SessionManager 会话生命周期重构：拆分 newSession 与 resetRuntimeState

### 重构 — 解除 resetSession 的「新建」与「重置」耦合，公共清理逻辑复用
- `SessionManager::resetSession()` 拆解为 `newSession()`（保存当前会话 + `createSession` +
  `resetRuntimeState`）与私有 `resetRuntimeState()`（清空内存 + 重建 system prompt +
  boundary_reset_hook_ + usage_refresh_hook_）；
- `reloadActiveSession()` 改用 `resetRuntimeState()`，使 `switchSession`/`deleteSession`
  复用同一套会话边界清理逻辑；
- `Agent::resetSession()` 与 `QmlBridge::newSession()` 相应改调 `newSession()`；
- 行为不变：空会话不新建（`!messages().empty()` 条件保留）、空会话仍触发 resetRuntimeState。

## [2026-08-03] TokenBudget 注入粒度细化：装配层只注入模型上限

### 重构 — 装配层不再依赖 TokenBudget 结构，仅注入真实模型上限值
- `Agent` 新增 `setModelLimit(int)`（内联，更新 `budget_.model_limit` 并刷新用量快照），
  移除 `setTokenBudget(TokenBudget)`；
- `AppAssembly::setupContextAndTokenBudget` 改为 `agent_.setModelLimit(client_.config().max_context_tokens)`，
  不再构造 `TokenBudget` 对象；
- `tests/test_agent.cpp` 相应改用 `setModelLimit(600)`；
- 说明：注入粒度从「整个 TokenBudget 对象」细化为「单个模型上限值」，装配层无需了解
  预算结构（warning/critical 阈值沿用默认值），与 Agent 解耦更彻底。

## [2026-08-03] 移除 Agent 类的 project_ 字段

### 删除 — Agent 不应持有 Project 引用，项目依赖已由工具层 ToolDependencies 独立接收
- 移除 `Agent::project_` 成员（`Agent.h`）及其 setter `setProject`；
- 移除 `AppAssembly::setupContextAndTokenBudget` 中的 `agent_.setProject(project_.get())` 调用；
- 移除 `Agent.h` 中不再需要的 `struct Project;` 前向声明；
- 说明：该字段仅有 setter 写入、Agent 内部从未读取（system prompt 构建在 `NovelAgentApp`，
  不经过 `project_`），为历史遗留死代码。解耦后 Agent 只关注 LLM 编排核心逻辑，
  项目上下文由工具层 `ToolDependencies` 独立注入。

## [2026-08-03] 移除内置工具禁用机制

### 删除 — 内置工具是 Agent 固定功能，无需用户禁用，禁用机制无实际需求
- 移除 `NovelAgentApp` 构造函数的 `disabledTools` 参数及 `setupAgent`/`registerBuiltInTools`
  透传链路；
- 移除 `BuiltInTool::registerAllTo` 的 `disabled` 参数与过滤逻辑，内置工具全部注册；
- 说明：该禁用机制自始为预留骨架（`QmlBridge` 调用点一直传空列表，无实际作用），
  且内置工具是 Agent 固定功能、普通用户无禁用入口，故整体移除，`registerBuiltInTools`
  恢复为无参全量注册。

## [2026-08-03] 保留描述中的双引号（单引号标量内为字面量）

### 修复 — save_skill 写盘不再剔除描述中的双引号
- `SkillTools::sanitizeFrontmatterValue` 移除双引号剔除分支：写盘已用 YAML 单引号标量包裹，
  双引号在单引号标量内是字面量，无需剔除，改为仅对单引号执行 `''` 翻倍、其余字符原样保留，
  避免描述被篡改（如 `她说"你好"` 变 `她说你好`）；
- 同步：`test_yaml_edge_cases` 往返断言补充双引号保留校验；`test_skill_registry` 10/10 通过。

## [2026-08-03] 移除技能 emoji 字段

### 删除 — 技能元数据不再包含可选图标
- 删除 `SkillMetadata::emoji` 字段及 frontmatter 的 `emoji` 键解析（一般技能不配置图标，
  该字段无实用价值）；
- 同步清理：`SaveSkillTool` 写盘不再输出 `emoji` 行、schema 参数移除，`SkillRegistry` 常驻
  技能标题不再加 emoji 前缀，`BuiltinSkills` 内置技能去掉 `emoji` 行，`QmlBridge` skillList 与
  `SkillPopup.qml` 展示移除 emoji，`skills/plot-structure/SKILL.md` 去掉 `emoji` 行；
- 同步：`test_frontmatter_parse` 移除 emoji 断言，`test_save_skill_injection` 移除 emoji 注入
  参数；`test_skill_registry` 10/10 通过。

## [2026-08-03] 编译链接启用死代码消除（gc-sections）

### 构建 — 减小可执行文件体积并略加速链接
- `cmake/CompilerSettings.cmake`：GCC/Clang 分支新增 `-ffunction-sections -fdata-sections`
  编译选项（每个函数/数据拆到独立节）与 `-Wl,--gc-sections` 链接选项（剔除未被引用的节），
  显著减小 exe 体积并让链接器处理更少输入；
- 与既有 `--icf=safe` 兼容；工具自注册（`REGISTER_TOOL`）依赖全局注册表引用，属正常符号
  引用，不会被 gc-sections 误删；
- 注意：新增编译选项会改变各 TU 的编译结果，首次全量重建时 ccache 命中清零（预期），
  后续增量构建恢复命中。

## [2026-08-03] 引入 yaml-cpp 解析 SKILL.md frontmatter

### 重构 — SkillLoader::parseFrontmatter 用 yaml-cpp 替换手写状态机
- `cmake/FetchDependencies.cmake` 新增 yaml-cpp 依赖：优先 `find_package(yaml-cpp)` 命中 MSYS2
  pacman 安装的版本（`mingw-w64-x86_64-yaml-cpp` 0.8.0，共享库），未安装则回退 FetchContent
  从 GitHub 浅克隆源码编译（固定 0.8.0 保证目标名 `yaml-cpp::yaml-cpp`）；
- `CMakeLists.txt`：`COMMON_LIBS` 追加 `yaml-cpp::yaml-cpp`，`NEEDED_DLLS` 追加
  `libyaml-cpp.dll`（MSYS2 为共享库，随 GUI 部署）；
- `SkillLoader::parseFrontmatter` 用 yaml-cpp 全量解析前以 `---` 分隔符提取文本块，字段集
  （name/description/emoji/always/commands）与既有行为完全一致，`always` 兼容布尔与字符串写法；
- `SaveSkillTool` 写盘时 `description`/`emoji` 改用 YAML 单引号标量包裹（首版双引号包裹
  经评审发现：双引号标量内反斜杠会触发转义解析导致写入非法 YAML，改单引号后反斜杠与
  裸值冒号均按字面量处理），`sanitize` 同步将单引号翻倍为 `''` 转义；
- `parseFrontmatter` 捕获 YAML 解析异常时直接抛异常（开发期不兼容存量，不符合协议即拒绝，
  不再逐行提取标量字段兜底），由 `discover` 捕获后跳过该技能并告警，不中断整体扫描；
- `always` 解析补齐 `on/On/ON` 写法（与 yaml-cpp 布尔语义一致）；
- 新增 `test_yaml_edge_cases` 覆盖非法 YAML 跳过、`always: on`、反斜杠写盘往返。
  `test_save_skill_injection` 恢复通过，`test_skill_registry` 10/10 通过。

## [2026-08-03] 移除技能环境门控体系（required_bins / required_envs / os_restrict）

### 删除 — SkillMetadata 环境门控字段
- 删除 `SkillMetadata::required_bins`、`required_envs`、`os_restrict` 三个字段及
  frontmatter 的 `required_bins`/`bins`、`required_envs`/`envs`、`os` 键解析；
- 删除 `SkillLoader::isBinaryAvailable`（内部经 `std::system` 调 `where`/`which`）、
  `isEnvAvailable`（`std::getenv`）与 `currentOS`（编译宏）——与已移除的 Shell 执行能力
  方向一致，纯小说创作技能无需依赖外部可执行文件、环境变量或平台差异；
- 移除 `discover` 阶段的 `checkGating` 门控过滤，技能一经发现即注册；
- 同步：`test_skill_registry` 注入用例改注入 `commands` 保持防护意图、`SkillTools` 注释、
  `docs/superpowers/plans/2026-07-30-yaml-cpp-integration.md` 字段集描述、`SkillMetadata` 注释
  （去除“门控”“跳过环境门控”残留表述）。

## [2026-07-30] 移除 Shell/PowerShell 工具

### 删除 — ShellTools（YAGNI + 安全面收缩）
- 删除 `src/agent/tools/ShellTools.h/.cpp`（`run_powershell` 工具）与 `tests/test_shell_tools.cpp`；
  工具靠 `REGISTER_TOOL_NP` 自注册，源文件删除即自动从注册表移除，无需改装配/注册代码。
- 理由：纯小说创作 Agent 用不到 Shell 执行能力，删除后减少提示注入下的安全面；
  同时消除独占 ~161.6s 的 test_shell_tools 慢测试（ctest 全量墙钟从 ~167s 降至 10s 量级）。
- 同步清理：`cmake/Sources.cmake`、`tests/CMakeLists.txt`（两处测试列表）、`CLAUDE.md`（安全规则/RAII 示例）。

## [2026-07-27] 真正的多会话 + Token 用量展示

### 新增功能 — 多会话存储与编排
- `SessionPersistence.h/.cpp`：全量重写为多会话布局 `.novelagent/sessions/index.json`（active + 会话元信息列表）+ `sessions/<id>.json`（消息数组）；
  新增 `SessionInfo` 结构体和 `listSessions()/activeSessionId()/createSession()/switchSession()/deleteSession()` API；
  首次 save 时从首条 user 消息提取自动标题（UTF-8 安全截断 30 字节）；
  删除会话时非空内容归档到 `archive/<id>.json`，删除 active 会话自动切到最近更新的剩余会话。
- `Agent.h/.cpp`：新增 `switchSession()/deleteSession()` 会话编排（先保存当前会话再切换/重载）；
  `resetSession()` 改为多会话语义（保存当前 + 新建空会话，当前会话为空时不新建避免堆积）。

### 新增功能 — Token 用量展示
- `Agent.h/.cpp`：新增 `ContextUsage` 缓存（total_tokens + percent），在 `process()` 成功、会话加载/切换/重置后通过 `refreshUsage()` 调用 ContextBudgetEvaluator 刷新。
- `NovelAgentApp.cpp`：TokenBudget 注入提前到 `loadSessionState()` 之前，启动恢复时百分比使用真实模型上限。
- `QmlBridge.h/.cpp`：`totalTokens/contextPercent` 属性接真实数据；新增 `sessionList()/switchSession()/deleteSession()` Q_INVOKABLE 和 `sessionsChanged` 信号。
- `SidebarPanel.qml`：会话列表从硬编码占位改为真实数据（整行点击切换、悬停删除按钮，HoverHandler 行高亮）。
- `AgentPanel.qml`：`onSessionReset` 改为 `reloadHistory()`，切换会话后加载目标会话历史。

### 重构 — 旧单会话 API 清理（YAGNI）
- `ProjectIO.h/.cpp`：删除 `loadConversation/appendConversation/saveConversation` 及 `kConversationJson` 常量；`createProjectDir` 不再预建 `conversation.json`。
- 旧单会话格式不做兼容迁移（当前阶段无存量用户数据需要兼容）。

### 测试
- `test_agent.cpp`：B2 重写为新 sessions 布局验证，新增 `contextUsage().total_tokens > 0` 断言。
- `test_context_manager.cpp`：新增 `test_multi_session_lifecycle`（新建/切换/删除/归档往返）。
- `test_project_io.cpp`：删除已无 API 对应的 `test_conversation`。
- 全量：23/23 通过（排除 deepseek|shell）。

## [2026-07-23] 删除并行编排相关代码

### 重构 — 移除并行处理路径
- `Agent.h`：删除 `useParallelProcessor()` / `isParallelEnabled()` / `parallel_mode_` / `processParallel()` 声明；
  删除 `#include "agent/AgentOrchestrator.h"` 和 `class TemplateManager;` 前向声明；
  清理 `orchestrator_` 成员。`processSerial()` 为唯一私有处理路径。
- `Agent.cpp`：删除 `initOrchestrator()`（原 `useParallelProcessor()`）；
  删除 `processParallel()` 函数；
  删除 `orchestrator_.reset()` 和 `parallel_mode_` 初始化；
  `process()` 中删除 if-else 分支，始终调用 `processSerial()`。
- `ReplHandler.cpp`：删除 `/parallel on|off` 命令；状态栏不再显示模式开关。
- `CHANGELOG.md`：追加本次记录。

## [2026-07-20] 用户取消机制实现（修复 #9 #11 + SSE 流式取消）

### Bug 修复
- `ToolCallLoop.cpp`：修复 #9 — `cancelled_` 检查移到循环开始处，不浪费 LLM API 调用；`chat()` 传入 `cancel_flag`；新增 `chat()` 返回后的二次检查。
- `ToolCallLoop.cpp`：修复 #11 — 取消退出路径正确设置 `rounds_executed`。
- `ReplHandler.cpp`：修复 Ctrl+C 在 `std::getline` 中导致 REPL 退出的问题，`clear()` 后继续循环。

### 新增功能 — 用户取消机制
- `ILLMClient.h` / `LLMClient.h`：`chat()` 新增 `const std::atomic<bool>* cancel_flag` 可选参数。
- `LLMClient.cpp`：SSE 流式回调中检查 `cancel_flag`，收到取消信号时 `return false` 中止 HTTP 连接，返回已累积的部分响应。
- `Agent.h`：新增 `cancel_requested_` 原子标志成员 + `requestCancel()` / `cancelFlag()` / `resetCancel()` 方法。
- `Agent.cpp`：`processSerial()` 中 `ToolCallLoop::setCancelled(&cancel_requested_)` 接入取消信号。
- `main.cpp`：注册 `SIGINT` 信号处理器，在 `runRepl()/runExec()` 期间将 `g_cancel_flag` 指向 Agent 的取消标志。
- `ReplHandler.cpp`：处理 `std::cin` 在 Ctrl+C 后的 fail 状态，显示取消提示。

### 测试适配
- `test_tool_call_loop.cpp` / `test_context_manager.cpp` / `test_sub_agent.cpp`：Mock `chat()` 签名新增 `const std::atomic<bool>*` 默认参数。

## [2026-07-20] 新增 QuantClaw 参考审查、消息队列计划

### 文档
- `docs/review/QUANTCLAW_REFERENCE_REVIEW_2026-07-20.md`：新增 QuantClaw 参考项目审查，记录两个发现：
  - **缺乏上下文溢出恢复**：LLM 返回 context overflow 时无重试机制，对比 QuantClaw 的 `CompactOverflow()` + 最多 3 次重试
  - **流式模式工具执行时机**：QuantClaw 在 stream callback 中收到 tool_call chunk 时即执行工具，不等流结束
- `docs/plan/QUEUE_QT_INTEGRATION_PLAN_2026-07-20.md`：新增消息队列 + Qt 前端集成计划
- `docs/review/TOOLCHAIN_AND_PARAMETER_VALIDATION_2026-07-20.md`：新增 ToolChain 功能记录 + LLM 参数传错问题分析

## [2026-07-19] 完整修复串行工具调用流程 15 个发现（#6-#15）

### Bug 修复
- `ContextManager.cpp`：compact LLM 调用的 token 消耗计入 TokenTracker（`tracker_.record()`），压缩后更新上下文快照为压缩后的新对话大小。
- `TokenCounter.cpp`：`countSingleMessage()` 和 `countMessages()` 新增 `reasoning_content` 字段的 token 统计。
- `Agent.cpp`：processSerial 中 `ToolCallLoopResult` 的 `cancelled`/`loop_detected` 标志传播到 `LLMResponse::finish_reason`。
- `ToolCallLoop.cpp`：`chat()` 调用和 `on_round_complete` hook 包裹 try-catch，异常时记录日志并重新抛出。修复 #10。
- `Agent.cpp`：`process()` 和 `execute()` 的 6 个提前返回路径（空输入/校验失败/状态拒绝/异常）均设置 `finish_reason`，调用方可区分错误类型。修复 #11。
- `Agent.cpp`：`buildEffectivePrompt()` 使用形参 `conversation` 而非成员 `conversation_`。修复 #12。

### 源码清理 & 优化
- `ToolCallLoop.h`：删除未使用的 `streaming` 字段和 `setStreaming()` setter。修复 #13。
- `ToolPipeline.h/.cpp`：删除 `executeAndAppend()`（无调用点）及其关联的 `conversation_` 成员和双参数构造函数。修复 #14。
- `ToolCallLoop.cpp`：`addAssistantFromResponse()` 的 tool_calls 拷贝处加警告注释，防止未来误改为 move。修复 #15。

### Bug 修复
- `ToolCallLoop.cpp`：最后一轮调用 chat() 时移除工具定义并追加提示词，避免 LLM 再请求工具调用无法结束。修复 #1（max_rounds 退出时 assistant 双重添加 + content 丢失）。
- `ToolCallLoop.cpp`：取消路径将本轮响应加入对话（有 tool_calls 时追加终止结果），后续 LLM 可见任务已被取消。零额外 token 开销。
- `ToolCallLoop.cpp`：正常路径中 `pipeline.execute()` 加 try-catch，异常时 `popBack()` 回滚已添加的孤立 assistant 消息。修复 #3。
- `Agent.cpp`：processSerial 最终 assistant 消息补全 `reasoning_content` 复制。修复 #4。
- `ToolCallLoop.cpp`：循环检测路径改为先加入 assistant(tool_calls) + 终止结果到对话（保证消息序列合法），再发一轮 chat() 通知 LLM 重复情况并获取最终文字答复。修复 #2（取消/循环检测退出丢弃有效响应）。

> 不再清理任何 assistant 消息的 reasoning_content（思考过程）。理由：① 实现复杂度归零；
> ② 保留推理过程不影响模型回复质量（实测保留时回复更连贯）；③ token 成本可忽略
> （~几百 token/轮，对比 1M 窗口九牛一毛）；④ 消除"何时/怎样 strip"的 bug 隐患。

### 源码清理 & 优化
- `Conversation.h`：删除 `stripReasoningContent()` 方法
- `Agent.cpp`：删除 `conversation.stripReasoningContent()` 调用及其注释
- `ContextManager.cpp`：压缩时在 compact prompt 中附带 `[思考过程]` 内容，避免摘要丢失推理中的关键信息
- `tests/test_e2e_reasoning_strip.cpp`：删除（专用测试不再需要）

### 行为变化
- 所有 assistant 消息的 `reasoning_content` 永久保留在对话中
- 不再做条件判断（有/无 tool_calls 均不处理）
- 与 DeepSeek API 规范一致（reasoning_content 回传不会导致 400）

## [2026-07-18] 删除反思（Reflection）机制

> REFLECTION_MECHANISM_REVIEW: ToolCallLoop 中的反思机制名不副实——检测到重复调用后仅注入模板消息就重新调 LLM，跳过工具执行、没有实际错误分析。每轮反思浪费一次 LLM 调用但无新信息，安全网由 `loop_detected` 终止保障。

### 源码清理
- `ToolCallLoop.h`：删除 `reflection_rounds_` 成员、`max_reflection_rounds` 字段+setter、`buildReflectionPrompt()` 声明、全部 `CRIT-2` 注释
- `ToolCallLoop.cpp`：删除 `buildReflectionPrompt()` 方法体；`has_repeated` 分支简化为直接 `loop_detected` 终止；删除 `reflection_rounds_ = 0` 重置和 `CRIT-2` 注释
- `ExecutionTracer.h`：删除 `ReflectionPayload` 结构体和 TracePayload variant 中条目（从未被任何代码 record）
- `ExecutionTracer.cpp`：删除 `ReflectionPayload` 序列化分支
- `test_tool_call_loop.cpp`：删除 `test_repeated_call_reflection()` 和 `test_reflection_exhausted()` 两个测试，更新文件头注释

### 行为变化
- 工具重复调用不再进入"反思→重试"循环，而是直接以 `loop_detected` 终止
- `isRepeatedCall()` 重复检测逻辑保留，仍可识别并终止死循环

## [2026-07-16] 删除 IMessageProcessor 模块，内联串行/并行处理逻辑到 Agent

> IMessageProcessor 策略模式存在 8 个属性与 Agent 完全重叠，6 个 setter 方法仅做转发
> 胶水，配置变更需四级传递。删除抽象层后架构更扁平，消除属性重复和配置传播代码。

### 架构变更
- **删除** `IMessageProcessor.h` / `IMessageProcessor.cpp`（~513 行）
- `Agent.h`：移除 `processor_` 成员和 `setProcessor()` 方法；新增 `parallel_mode_` 标志和
  `orchestrator_` 成员；新增 `processSerial()` / `processParallel()` / `buildEffectivePrompt()`
  私有方法
- `Agent.cpp`：`processUserMessage()` 改为根据 `parallel_mode_` 标志 if-else 选择处理路径；
  内联原 SerialProcessor::process()、ParallelProcessor::process()、buildEffectivePrompt()
  实现；消除全部配置传播代码（setSystemPrompt/setMaxToolRounds/setContextManager/
  setMaxContextTokens 不再同步到 processor）

### 源码清理
- `cmake/Sources.cmake`：删除 IMessageProcessor 构建条目
- `ReplHandler.cpp`：删除 `/config max_context_tokens` 中通过重建 processor 同步配置的 hack
- `SessionManager.cpp`：删除冗余的 `useSerialProcessor()` 调用（Agent 构造函数已默认串行模式）
- `NovelAgentApp.cpp`：更新构造注释

## [2026-07-16] 重命名 effective_prompt → effective_system_prompt

> NAMING_ISSUES_REVIEW: `effective_prompt` 命名歧义已修复，明确其角色为"系统提示词"。

### 源码清理
- `IMessageProcessor.cpp`：`SerialProcessor::process()` 和 `ParallelProcessor::process()` 中重命名
- `Agent.cpp`：`Agent::execute()` 中重命名

## [2026-07-16] 删除 ToolCallLoop 中所有 tracer 记录

> 删除 ToolCallLoop 中全部的 tracer_->record() 调用、tracer_ 成员变量、
> 构造函数参数，以及相关的 ErrorPayload/ReflectionPayload/ToolCallPayload 引用。

### 源码清理
- `ToolCallLoop.h`：删除 `#include ExecutionTracer.h`、tracer 构造函数参数、tracer_ 成员
- `ToolCallLoop.cpp`：删除全部 10 处 tracer_->record() 调用
- `IMessageProcessor.cpp`：更新 ToolCallLoop 构造调用（去掉 tracer 参数）
- `SubAgent.cpp`：更新 ToolCallLoop 构造调用（去掉 tracer 参数）

## [2026-07-16] 删除 ToolCallLoop 中不必要的计时代码

> 移除 `ToolCallLoop::run()` 内部全部 8 次 `steady_clock::now()` 调用，
> round_ms/tool_ms 计时仅 tracer 一个消费者，无 tracer 时完全浪费。

### 源码清理
- `ToolCallLoop.cpp`：删除首轮、正常循环路径、反思路径三处的计时代码和 round_ms/tool_ms 计算
- `ToolCallLoop.cpp`：删除冗余的 `#include <chrono>`

## [2026-07-16] 移除 `initial_messages` 参数

> 删除 `ToolCallLoop::run()` 的 `initial_messages` 参数及相关代码，此功能已因预思考代码清理和 ContextManager 直接修改 conversation 而不再需要。

### 源码清理
- `ToolCallLoop.h`：从 `run()` 签名中移除 `initial_messages` 参数
- `ToolCallLoop.cpp`：删除三元表达式，首轮直接使用 `conversation.messages()`
- `IMessageProcessor.h`：`buildEffectivePrompt()` 移除 `out_messages` 传出参数
- `IMessageProcessor.cpp`：删除 `effective_messages` 局部变量，重构 `buildEffectivePrompt()` 签名

## [2026-07-16] 清理预思考代码（A4 use_thinking_step）

> 删除 `use_thinking_step` 字段及相关代码块，清理关联 review 文档，为 Plan Mode 从零设计扫清障碍。

### 源码清理
- `ToolCallLoop.h`：删除 `use_thinking_step` 字段及注释
- `ToolCallLoop.cpp`：删除预思考步骤代码块（A4 ReAct 思考阶段）

### 文档同步
- `docs/design/plan_mode.md`：标记为 obsolete（旧代码已清理，待重写）
- `docs/review/REVIEW_STATUS.md`：A4 行更新为"已清理"
- `docs/review/INITIAL_MESSAGES_REVIEW_2026-07-16.md`：更新预思考相关引用
- `docs/review/PLAN_MODE_CLEANUP_PLAN.md`：内容已合并到 REVIEW_STATUS.md，文件删除

## [2026-07-16] 架构审查文档整理 + 注释修正

> 系统提示词所有权审查、initial_messages 参数审查、命名问题审查等三份文档写入 `docs/review/`。
> 将 `PLAN_MODE_CLEANUP_PLAN.md` 从 design 移至 review。
> 更新 `IMessageProcessor.h` 中 ContextManager 过时注释（移除"RAG 检索"引用）。

### 新增审查文档
- `docs/review/SYSTEM_PROMPT_OWNERSHIP_REVIEW_2026-07-16.md`：系统提示词三副本问题分析与 Conversation 统一管理决议
- `docs/review/INITIAL_MESSAGES_REVIEW_2026-07-16.md`：`ToolCallLoop::initial_messages` 参数陈旧快照、死代码等问题分析
- `docs/review/NAMING_ISSUES_REVIEW_2026-07-16.md`：`effective_prompt` 命名歧义分析与修复建议

### 文档整理
- `docs/design/PLAN_MODE_CLEANUP_PLAN.md` → `docs/review/PLAN_MODE_CLEANUP_PLAN.md`（按 review 分类归档）
- 删除 `docs/design/thinking_step_detector.md`（已纳入 plan_mode.md 不再独立维护）

### 注释修正
- `IMessageProcessor.h`：ContextManager 成员注释更新，移除过时"RAG 检索"描述，改为准确职责：动态 system prompt / Token 追踪 / 对话压缩 / 会话持久化

## [2026-07-16] 修复工具注册缺失 + 清理架构审查文档

> 修复 4 个工具缺少 REGISTER_TOOL 宏导致的静默失效（LLM 被指导使用却永远收到"工具未找到"）。
> 更新架构审查文档，已修复项移至新文件 ARCHITECTURE_REVIEW_RESOLVED.md，仅保留 3 项待处理。
> 删除过时分析文档：MAYBE_AUTO_COMPACT.md（全索引模式已解决）、TOOL_REGISTRATION_GAP.md（已修复）。
> 新增设计文档：Plan Mode 用户可控预思考步骤、A4 条件化 Thinking Step Detector。

### Bug 修复
- `ChapterContextTools.cpp` / `RelevantCharacterTools.cpp` / `RelevantSettingTools.cpp` / `RelevantWorldRuleTools.cpp`：4 个工具补充 `REGISTER_TOOL` 宏，消除编译有定义但运行时不可用的静默失效

### 文档清理
- `ARCHITECTURE_REVIEW_2026-06-30.md`：精简为仅含 B2/C1/C4 三项待处理问题，其余已修复/已关闭
- `REVIEW_STATUS.md`：新增第六节（架构审查 11 项分类统计）、第七节（工具注册缺失审查结论）
- 删除 `MAYBE_AUTO_COMPACT.md`（章节切换自动压缩设计问题，已通过全索引模式解决）
- 删除 `TOOL_REGISTRATION_GAP.md`（工具注册缺失记录，已修复）

### 代码微调
- `ToolCallLoop.cpp`：注释缩进对齐
- `Conversation.h`：`pinned_indices` 注释修正为明确索引范围为 `diff.added` 内部

### 新增设计文档
- `docs/design/plan_mode.md`：Plan Mode 用户可控预思考步骤设计（状态：待审查）
- `docs/design/thinking_step_detector.md`：A4 条件化 Thinking Step Detector 重构设计（状态：待审查）

## [2026-07-15] 修复 DeepSeek reasoning_content 丢失问题 + 可配置 Thinking 模式

> 修复了三处 reasoning_content 丢失问题：(1) Message 结构体缺少字段，
> (2) ToolCallLoop 不复制 reasoning_content，(3) buildRequestBody 未请求思考模式。
> 新增 ProviderConfig 中 enable_thinking / reasoning_effort 可配置项。

### 核心改动
- `Message.h`：新增 `reasoning_content` 字段及 to_json/from_json 序列化
- `ToolCallLoop.cpp`：带 tool_calls 路径下复制 reasoning_content 到 assistant 消息
- `AppConfig.h`：ProviderConfig 新增 `enable_thinking`（默认 false）+ `reasoning_effort`（默认 "high"）
- `LLMClient.cpp`：buildRequestBody 中根据 enable_thinking 添加 thinking 参数
- `test_app_config.cpp`：新增 3 个 thinking 配置测试
- `test_tool_call_loop.cpp`：新增 reasoning_content 保留测试

### 设计决策
- reasoning_content 不持久化到 conversation.json（仅内存保留）
- enable_thinking 默认关闭（opt-in，避免 token 浪费）
- reasoning_effort 默认 "high"

## [2026-07-13] 工具自注册宏灵活化：新增 REGISTER_TOOL_NP 消除手动样板代码

> 新增 `REGISTER_TOOL_NP` 宏，用于不需要 `Project` 指针的工具注册。
> 将 `ShellTools.cpp` 和 `SearchMemoryTools.cpp` 中的手动注册块替换为单行宏调用。

### 核心改动
- `BuiltInTool.h`：新增 `REGISTER_TOOL_NP(ToolClass, toolName, varSuffix)` 宏，工厂 lambda 构造工具时不传 Project 参数
- `ShellTools.cpp`：手动注册（9 行）→ `REGISTER_TOOL_NP`（1 行）
- `SearchMemoryTools.cpp`：手动注册（9 行）→ `REGISTER_TOOL_NP`（1 行）
- `CLAUDE.md`：更新工具自注册章节，说明两个宏的适用场景
- `SearchMemoryTools.h`：注释更新

### 影响范围
- 34 处现有 `REGISTER_TOOL(...)` 零改动
- `BuiltInTool::Factory` 签名不变，`registerAllTo` 不变
- 净删 ~14 行样板代码，净增 ~12 行宏定义

## [2026-07-12] 合并 ContextManager::assemble() 自动压缩和告警为多级决策 + 四级阈值体系

> 将 assemble() 中步骤 2（自动压缩检查）与步骤 3（阈值告警）合并为统一的多级决策块，
> 用 checkThresholds() 作为单一入口，消除重复计算和压缩成功后仍产生误导性告警的问题。
>
> 引入 Error/AutoCompact 状态，四级可配置阈值体系：Warning(60%) → Critical(85%) → AutoCompact(95%) → Error(>100%)。
> 阈值从硬编码改为可配置，auto_compact_threshold_ 从 ContextManager 移至 TokenTracker 统一管理。

### 核心改动
- `ContextManager::assemble()`：合并步骤 2+3；自动压缩条件改为 `pre_check.status >= AutoCompact`；四级 switch 告警
- `ContextStatus` 枚举：`Normal/Warning/Critical` → `Normal/Warning/Critical/AutoCompact/Error`
- `ContextAssembly`：新增 `bool fatal` 标志，Error 状态下置位供调用方中断请求
- `TokenTracker`：新增三个可配置阈值字段（warning/critical/auto_compact）+ setter/getter；`check()`/`check(int)` 四级判定替代硬编码 60/85
- `ContextManager`：删除 `auto_compact_threshold_` 字段（阈值归属 TokenTracker）；`setAutoCompact` 默认值 70→95；新增 `setWarningThreshold`/`setCriticalThreshold`/`setAutoCompactThreshold` 转发接口

### 注释同步
- `ContextManager.h`：assemble() 处理流程由 5 步改为 4 步
- `ContextManager.cpp`：assemble() 流程注释块同步更新为 4 步结构

### 测试
- `test_critical_warning`：从搜索 "接近模型上限"（Critical）改为 "超过模型上限"（Error）+ 验证 `result.fatal`

## [2026-07-11] 合并 Compactor 到 ContextManager + 修正自动压缩触发时机

> Compactor 类展开合并到 ContextManager，消除 1:1 转发方法和重复字段同步代码。
> 自动压缩检查从 Agent（请求前，读陈旧数据）移到 assemble() 内部（实时 total_tokens），
> 解决新用户输入可能导致超阈值但检查过早无法捕获的问题。

### 核心改动
- `ContextManager`：合并 Compactor 全部成员和方法（`summary_`, `marker_`, `auto_compact_`, `compact()` 等），删除 `compactor_` 转发层
- `ContextManager::assemble()`：新增步骤 2.5 自动压缩检查，基于本轮实时 `total_tokens` 判断，修正了此前用 `tracker_.usagePercent()`（上一轮陈旧数据）的问题
- `ContextManager::assemble()` 签名：`const Conversation&` → `Conversation&`（压缩需修改对话），新增 `ILLMClient*` 参数（默认 nullptr）
- `Agent::processInput()`：删除步骤 4 早期自动压缩检查（已由 assemble 内部接管）
- `setCalibrator`/`setModelName`/`setModelContextLimit`：删除 `compactor_.setXxx()` 同步调用，字段只存一份

### 删除的文件
- `src/agent/Compactor.h`、`src/agent/Compactor.cpp`：合并到 ContextManager
- `tests/test_compactor.cpp`：核心测试已叠加覆盖

### 消除的冗余
- `kCompactKeepExchanges` / `kCompactSystemPrompt`：两份重复常量合并为一份
- `calibrator_` / `model_name_` / `model_context_limit_`：不再在 Compactor 中重复存储
- 9 个 1:1 转发方法：变为直接成员访问

## [2026-07-11] 删除 truncateMessages 截断机制

> 截断丢弃旧消息永久丢失信息，与 compaction（摘要保留）的哲学相悖。
> `shouldAutoCompact()` 在 70% 阈值主动压缩，截断安全网几乎不会触发。

### 核心改动
- `ContextManager::assemble()`：删除步骤 3 截断，全部消息直接通过
- `ContextManager`：删除 `truncateMessages()`、`lastTruncatedCount()`、`truncated_count`
- `IMessageProcessor`：删除依赖 `lastTruncatedCount()` 的同步压缩逃生阀
- 测试：移除 6 个截断相关用例

### 文档
- `COMPACTOR_PROJECT_REF_TRUNCATION.md` 状态更新：9/12 已解决（原 8/12）

## [2026-07-11] 重构：压缩摘要从 system prompt 迁移到对话消息

> 将压缩摘要从 `assemble()` 步骤 1 中注入 system prompt 改为在 `compact()` 时
> 以 user/assistant 消息对直接插入对话列表头部。解决三个问题：
> 1. 语义错位 — 摘要是"事实"而非"指令"，不应放在 system prompt 中
> 2. API 缓存 — system prompt 不再因摘要变化而失效
> 3. 永久累积 — 旧摘要现在在消息列表中，下次 compact() 能被重新压缩

### 核心改动
- `Compactor::compact()`：删除旧消息后插入 `addUser("【系统】以下是被压缩的旧对话摘要：")` + `addAssistant("[被压缩的历史摘要]\n" + 摘要)`
- `ContextManager::assemble()`：删除步骤 1 摘要注入代码（不再读 `compactor_.summary()`），重编号步骤 1-6
- `ContextManagerTypes.h`：删除 `ContextAssembly::has_compacted_context`（无消费者）
- `kCompactSystemPrompt`：增加旧摘要提示，防止多次压缩信息衰减

### 文档
- `COMPACTOR_PROJECT_REF_TRUNCATION.md` 状态更新：8/12 已解决（原 7/12）

## [2026-07-11] Phase 3.7 补充：删除 assemble() 自动向量检索

> 删除 `ContextManager::assemble()` 中每轮自动执行向量检索并注入 system prompt 的逻辑。
> 理由与该阶段已删除的 `buildProjectRef` / `maybeAutoCompact` 相同：代码替 LLM 做决定——不可控、
> 质量不高、导致 system prompt 膨胀。LLM 通过 `search_memory` 工具按需搜索，结果在对话消息中而非 system prompt。

### 代码清理
- `ContextManager::assemble()` 删除步骤 1 自动向量检索块（~60 行）
- 删除 `setRetrievalBackend` / `isVectorStoreStale` / `clearVectorStore` / `hasRetrievalBackend` 接口
- 删除 `vector_store_` / `embedding_gen_` / `retrieval_top_k_` / `vector_store_dirty_` 成员变量
- 删除 `SessionMeta::vector_store_dirty` / `ContextAssembly::has_semantic_context` 字段
- 删除 `Agent::rewindTo` 中对 `clearVectorStore` 的调用
- 删除 `test_isVectorStoreStale_reads_novel_json` 测试（`test_context_manager` 23→22 测试）

### 文档
- `COMPACTOR_PROJECT_REF_TRUNCATION.md` 状态更新：7/12 已解决（原 6/12）

## [2026-07-10] 注释清理：移除 Doxygen 标记

> 移除所有 `@param`、`@return`、`@note`、`@warning`、`@brief`、`@throws` 等 Doxygen 标记，
> 与已统一为 `//` 的注释风格保持一致。

### 代码清理
- 31 个文件中的 `@param`/`@return`/`@note`/`@warning`/`@brief`/`@throws` 标记已移除

## [2026-07-10] 注释风格统一：/// → //

> 将项目中所有 `///` 注释统一替换为 `//`，消除 Doxygen 风格的多斜杠注释。

### 代码清理
- 批量替换 `src/` 和 `tests/` 下全部 100 个 .h/.cpp 文件中的 `///` 为 `//`
- 涉及注释风格包括：文件头注释、函数说明、参数注释、行内 `///<` 注释等

## [2026-07-10] 注释清理：移除 //< 中的 < 符号

> 清除行内注释中残留的 `<` 符号，统一注释风格。

### 代码清理
- 15 个文件中的 `//<` 替换为 `//`

## [2026-06-30] 审查修复补充：测试全覆盖 + DirtyBit 防护 + Shell 扩展

> 基于设计审查修复评估报告，按优先级完成全部遗留修复项。
> 全量 22/22 通过。

### 测试覆盖（新增 4 个测试套件 + 22 个用例）
- `test_setting_tools.cpp`（6 用例）：create/get/get_all/update/delete+级联/错误处理
- `test_world_rule_tools.cpp`（5 用例）：create/get/update/delete+级联/错误处理
- `test_outline_tools.cpp`（7 用例）：get/create_volume/update_volume/create_plot_thread/update_plot_thread/get_project_status/错误处理
- `test_style_tools.cpp`（5 用例）：read_default/update_string/update_int/update_array/空fields错误
- `test_shell_tools.cpp`：新增扩展白名单测试（Get-Process/Get-Service/Get-Acl/Get-Member/Write-Output/别名）

### DirtyBit 防护（Issue 5 安全加固）
- `ProjectIO::save()`：dirty_flags==0 但有子实体时全量保存 + 告警，防止新增工具漏调 markDirty()

### ShellTools 白名单扩展
- 新增 `get-process`/`get-service`/`get-itemproperty`/`get-acl`/`get-member`/`write-output`/`write-host` 及别名（`ps`/`echo`/`gp`/`gl`）

### StyleTools 修正
- `UpdateStyleTool::parameters()`：空 fields schema → 完整列出 22 个可更新字段（C5 修复遗漏）
- 修复由此导致的 C4 校验误阻断 LLM 传入合法字段

### 代码清理
- `ChapterTools.cpp`：删除 `AppendChapterTool` 中重复的 return 语句（死代码）

> 对 DESIGN_REVIEW.md 全部 35 项已修复条目做逐项源码复核（三路并行核实 + 人工二次确认）。
> 复核结论：核心修复（B1/B2/B3/B8/A1/A2/A4/A5/A7/A8/D2/A15 等）均已实质到位且有测试；
> 发现 3 项遗留短板并直接修复，新增 1 个测试可执行文件 + 3 个测试用例。
> 全量 17/17 通过（test_shell_tools 为本次新增）。

### A6 — 跨实体引用校验覆盖补齐（部分修复→修复）
- 复核发现评审 A6 点名的 `Relationship.target_character_id` 在 `update_character_relationships` 中未校验；
  `update_chapter_scenes` 的 pov_character_id/participants/location_id/plot_thread_ids、
  `update_volume` 的 start/end_chapter_id/focus_characters/active_plot_threads 均未校验。
- `CharacterTools.cpp`：`update_character_relationships` 补 target_character_id 软校验
- `ChapterTools.cpp`：新增 `validateSceneRefs` helper，`update_chapter_scenes` 解析每个场景后校验 4 类引用
- `OutlineTools.cpp`：`update_volume` 字段循环补 start/end_chapter_id + focus_characters + active_plot_threads 校验
- 顺带清理 ChapterTools.cpp 未使用的 `validateChapterId`/`validatePlotThreadId`（消除 -Wunused-function 警告）

### C1+C2 — Shell 白名单绕过收紧 + 段内参数解析 bug 修复
- 复核发现 `foreach-object` 在白名单中可执行脚本块 `{ ... }`，且注入拦截未覆盖 `{}`/`;`/`&`
- `ShellTools.cpp`：移除 `foreach-object`；入口拦截 `{` `}` `;` `&` 四类脚本注入字符
- **修复预存 bug**：原 token 解析把段内参数（如 `Get-Content config.json` 的 `config.json`）也当 cmdlet 校验，
  导致任何带参数的命令都被误拦——白名单"更严但不可用"。改为只校验每段首个 token，跳过段内参数

### 文档
- `ISynthesisStrategy.h`：`LlmSynthesis` 注释 800→3000（实际默认值已改但注释未同步）
- `DESIGN_REVIEW.md`：更新 A6/C1+C2 状态与复核结论

### 测试
- 新增 `tests/test_shell_tools.cpp`（5 用例，黑盒验证白名单放行/拦截/注入字符/管道段/空命令）
- `test_chapter_tools.cpp`：加 `update_chapter_scenes` 悬空引用软校验用例
- `test_character_tools.cpp`：加 `update_character_relationships` 写入 + 悬空 target 软校验用例
- `test_context_manager.cpp`：加 A1 compact 真删消息用例（30→20 条）+ 消息不足跳过用例
- 全量 17/17 通过

## [2026-06-28] 设计评审批次⑤（最终轮）：语义检索激活 + 顺手修复（A2/A3/A9/A11/A13/A14）

> 最后一轮。A2+A3 是整个评审最大的"承诺 vs 现实"落差——向量检索子系统代码全写好了但从未接入。
> 顺带修 A9 设定消息自动 pin、A13 word_count 假数据、A14 CharacterDevelopment 通道、A11 NovelChunker 中文适配。

### A2+A3 — 语义检索管线激活
- `ReplHandler.cpp`：`/index` 命令从桩变成真实现（遍历章节/角色/设定/规则→NovelChunker切分→EmbeddingGenerator生成嵌入→VectorStore.insert→saveToFile持久化）
- `NovelAgentApp.h`：暴露 `vectorStore()` 和 `embeddingGenerator()` accessor
- `ReplHandler.h`：加 `setApp(NovelAgentApp*)` 后向引用
- `VectorStore.h`：`saveToFile()` 从 private 改为 public
- `ContextManager.cpp`：assemble() 语义召回加 chapter_id 去重 + 分层标签 "[补充记忆]"
- `ContextManagerTypes.h`：`ContextAssembly` 加 `has_semantic_context`

### A11 — NovelChunker 修复
- `NovelChunker.cpp`：4个工厂方法 metadata 加 `{"text", text}` 字段（修复两个消费者找不到 text 导致整条召回链路白费的 bug）
- `NovelChunker.cpp`：`overlapFromPrevious` 加 UTF-8 安全截断回退（续字节 0x80-0xBF 检测）
- `EmbeddingGenerator.cpp`：`preprocessText` 加 UTF-8 安全截断回退

### A9 — 设定消息自动 pin
- `ToolPipeline.cpp`：`executeAndAppend` 加设定工具白名单，执行后自动 `pinMessage`

### A13 — word_count 自动维护
- `ChapterTools.cpp`：`WriteChapterTool` 和 `AppendChapterTool` 写完正文后用 TokenCounter 更新 `Chapter::word_count` 和 `Project::current_word_count`

### A14 — CharacterDevelopment 通道
- `CharacterTools.h/.cpp`：新增 `AddCharacterDevelopmentTool`（参数 character_id/chapter_id/summary/category/affected_fields，ID=dev-char_id-N）

### 测试
- 全量 16/16 通过

## [2026-06-28] 设计评审批次④：状态机实现 + Shell 白名单 + 并行编排修复（D1/C1C2/A18）

> 将三项"装饰/过度/误判"补建成真正可用。全量 16/16 通过。

### C1+C2 — Shell 黑名单→白名单
- `ShellTools.cpp`：52 条黑名单 → 16 安全 cmdlet 白名单 + token 级管道解析；拦截 ` $() `` ` `. ` 注入
- `ShellTools.h`：description 更新为"只读 PowerShell 查询命令"

### D1 — 状态机可用
- `ToolCallLoop`：工具执行前后 `transition(AwaitingTool/Thinking)`
- `AgentState.cpp`：`AwaitingTool→Idle` 合法化
- `ChapterTools.cpp`：`WriteChapterTool` 覆写前检查（`allow_auto_overwrite=false` 时返回 confirm_overwrite）
- `Project.h`：加 `allow_auto_overwrite` 字段

### A18 — 并行编排修复
- `KeywordParallelDetector`：负向规则 + 双关键词联合命中
- `SubAgentTemplate`：`suggested_max_rounds` 差分配（3-8）
- `ParallelProcessor`：补 `ContextManager::assemble()` 注入
- `LlmSynthesis`：`max_result_chars` 800→3000

## [2026-06-28] 设计评审批次②：修复内存安全与死锁（B3/B5/B8）

> 依据 `docs/review/DESIGN_REVIEW.md` 评审报告，修复第二批「内存安全/死锁」类高严重度问题。

### B3 — SubAgent 超时 use-after-free
- `src/agent/SubAgent.cpp`：`execute()` 超时后改为 `future.wait()` 无条件等待异步任务彻底退出，替代"放弃等待"（此前在清理宽限期过后直接返回销毁 this，异步线程访问已析构对象导致悬空）。
- 注释自承认的逻辑已消除。最坏情况等待 HTTP read_timeout（180s），远好于 use-after-free。
- 新增 `test_sub_agent` 1 项 B3 验证（SlowMockLLMClient 睡眠 8s + 1s 超时 → 不崩溃返回 timed_out）。

### B5 — 主循环超时保护
- `src/agent/IMessageProcessor.cpp`：`SerialProcessor::process()` 中为 `ToolCallLoopConfig` 设置 `timeout=300s`（5 分钟），防止 HTTP 半开/服务端卡死时主线程永久阻塞。
- 此前 SerialProcessor 不设 timeout（默认 0→同步模式无超时），SubAgent 有 120s 超时主循环反而没有——保护不一致已修复。

### B8 — 异常后状态恢复（不卡 Thinking 永久拒输入）
- `src/agent/Agent.cpp`：`processUserMessage()` 对步骤 6-7.5（处理器调用 + 状态恢复 + 轨迹记录 + 增量保存）加 try-catch 包裹。捕获异常后强制 `transition(Error) → recover() → Idle`，返回空响应。
- 此前异常穿透到 ReplHandler 的 catch，`state_` 卡在 Thinking 导致 `canAcceptInput()` 永久返回 false 直至重启——死锁路径已消除。
- 新增 `test_agent` 1 项 B8 验证（mock 返回非法 JSON → 异常后 canAcceptInput() 为 true，状态为 Idle）。

### 顺带修复 — ContextManager token 阈值测试 3 项既有失败
- `src/agent/ContextManager.cpp`：`recordUsage()` 同步更新 `current_context_size_ = input_tokens`（API 返回的 prompt_tokens 比启发式估算更精确）。
- 修复后 `test_context_manager` 中 `usagePercent`/`checkThresholds`/`has_critical` 三项由失败变通过。

### 测试
- 全量 **16/16 全部通过**（test_context_manager 遗留失败已消除）。
- 新增：`test_sub_agent` B3 超时测试、`test_agent` B8 异常恢复测试。

## [2026-06-28] 设计评审批次①：堵住数据丢失与静默失效（B1/A5/D2/B2）

> 依据 `docs/review/DESIGN_REVIEW.md` 评审报告，修复第一批「会丢用户数据 / 静默失效」的高严重度问题。
> 范围：可靠性底线性修复，零风险高收益，不涉及架构改动。

### B1 — writeText 原子化（temp + rename）
- `src/utils/FileUtils.cpp`：`writeText` 改为先写同目录临时文件 `<path>.tmp.<seq>`，flush 落盘后 `fs::rename` 原子替换目标，rename 失败回退「删目标再 rename」。
- 覆盖所有持久化路径（ProjectIO 6 个 JSON / SessionPersistence / VectorStore / writeChapter / ExecutionTracer），崩溃写到一半不再产生半截损坏文件（原 B6 vectors.json 一并受益）。
- 新增 `tests/test_file_utils.cpp`（6 项：往返/覆盖/无临时残留/父目录自动创建/大内容完整/缺失文件返回空）。

### A5 — project.json 错路径改为 novel.json
- `src/agent/ContextManager.cpp`：3 处 `last_write_time(".../project.json")` 写死路径改为引用导出常量，抽取 `projectSettingsMtime()` helper。
- `src/project/ProjectIO.h`：导出文件名常量 `kNovelJsonFileName` 等（单一来源），`ProjectIO.cpp` 内部短名常量改为引用导出常量，消除字面量漂移根源。
- 修复后 `isVectorStoreStale()` 与「Project 修改后清空旧摘要」的 mtime 一致性保障恢复正常（此前因文件名不匹配整条静默失效）。
- 顺手修正过时注释：`SessionPersistence.h`、`Project.h` 的 project.json → novel.json。
- 新增 `test_context_manager` 2 项 A5 验证（isVectorStoreStale 读 novel.json mtime / saveSessionState 记录非零 mtime）。

### D2 — config context_window 字段迁移兼容
- `src/config/AppConfig.h`：`ProviderConfig` 弃用 `NLOHMANN_DEFINE_TYPE_INTRUSIVE`，手写 `to_json`/`from_json`。
- `from_json` 优先读新字段 `max_context_tokens`，缺失时回退读旧字段 `context_window`（commit 51b7616 重命名后未迁移旧 config.json，致用户配置静默失效）。
- `to_json` 只写新字段名，保存时自动升级格式。
- 新增 `tests/test_app_config.cpp`（6 项：旧字段读取/新字段读取/新旧优先/默认值/升级保存/旧 config 往返升级）。

### B2 — 会话增量保存
- `src/agent/Agent.cpp`：`processUserMessage` 末尾（maybeAutoCompact 后）调用 `saveSessionState()`，每轮对话落盘到 conversation.json + session_meta.json，写入失败 try/catch 不阻断主流程。
- 此前仅在 REPL 退出时保存一次，长会话写作中途崩溃会丢失本轮全部对话与创作上下文。
- 删除从未被调用的死代码 `NovelAgentApp::saveConversationIfNeeded`（.h 声明 + .cpp 空壳定义）。
- 新增 `test_agent` 1 项 B2 验证（processUserMessage 后 conversation.json 含 user+assistant 两条）。

### 测试
- 新增测试套件：`test_file_utils`、`test_app_config`（均注册进 tests/CMakeLists.txt）。
- 全量 16 个套件 15 通过；test_context_manager 仍有 3 项开工前既有的 token 阈值计算失败（usagePercent/checkThresholds/has_critical），与本批次无关。

## [2026-06-28] 修复 Model 字段 LLM 写入能力缺口：新增 update_chapter / create_setting / create_world_rule / update_style + 扩展 create_chapter / create_character

### 新增工具
- **`update_chapter`** — 更新章节创作简报字段（15 string + 7 array 白名单，仿 update_character 模式）
- **`create_setting`** — 创建世界观设定（setting-001 格式 ID，6 个可选叙事字段）
- **`create_world_rule`** — 创建世界规则（rule-001 格式 ID，5 个可选字段）
- **`update_style`** — 更新写作风格配置（19 string + 1 int + 3 array 白名单，Style 单例无需 id）

### 工具扩展
- **`create_chapter`** — 参数从 2→16，创建时可填充 goal/conflict/hook 等叙事简报字段
- **`create_character`** — 参数从 2→12，创建时可填充 personality/background/goal 等
- **`update_setting`** — 新增 related_characters/related_plot_threads/related_rule_ids/tags 数组字段
- **`update_world_rule`** — 新增 related_settings/tags 数组字段

### Bug 修复（代码审查）
- 修复 `UpdateChapterTool` 的 `const_cast` 脆性：`findChapter` 改为返回非 const 指针
- 修复 `UpdateStyleTool` int 字段处理器：`kIntFields` set 改为 `kIntMap` ptr-to-member map，消除潜在幽灵 bug
- 修复 `UpdateCharacterTool` 白名单缺失：`tags` 加入 `kArrayMap`
- 修复 `CreateCharacterTool` 头文件注释：更新为反映完整 12 参数

## [2026-06-28] 新增 LLM 主动查询工具：search_memory / read_style + 扩展 get_project_status

### 新增工具

- **`search_memory`** — 显式语义搜索工具，允许 LLM 用自定义 query 主动查询向量存储
  - 文件：`src/agent/tools/SearchMemoryTools.h/.cpp`
  - 注册方式：手动工厂（仿 ShellTools，不接收 Project&）
  - 后端注入：`initSearchMemoryBackend()` 在 `NovelAgentApp::setupAgent()` 中调用
  - 参数：`{query: string, top_k?: integer(默认5)}`
  - 分类：`ToolCategory::System`
- **`read_style`** — 写作风格查询工具，返回完整 Style 配置（24 字段）
  - 文件：`src/agent/tools/StyleTools.h/.cpp`
  - 注册方式：标准 `REGISTER_TOOL` 宏
  - 分类：`ToolCategory::Setting`

### 工具扩展

- **`get_project_status`** — 从 8 字段扩展到 23 字段
  - 新增：`description`, `genre`, `comps`, `central_question`, `ending_type`, `target_word_count`, `current_word_count`, `status`, `must_have_elements`, `must_avoid_elements`, `narrative_promises`, `tags`, `world_rules_summary`, `created`, `modified`

### 构建

- `cmake/Sources.cmake` — `NOVELAGENT_TOOLS` 追加 SearchMemoryTools 和 StyleTools 文件对
- `src/NovelAgentApp.cpp` — `setupAgent()` 中在 `registerAllTools()` 前调用 `initSearchMemoryBackend()`

## [2026-06-25] 3 项代码审查修复 + 上下文模块注释补充

### Bug 修复（f64c304 提交后审查）

- **修复** — `vector_store_dirty_` 未持久化到 `SessionMeta`：`/rewind` 后重启应用导致向量脏标记丢失
  - `SessionMeta` 新增 `vector_store_dirty` 字段
  - `saveMeta()` / `loadMeta()` 对称序列化/反序列化
  - `saveSessionState()` / `loadSessionState()` 写入/恢复该字段
- **修复** — `Agent::execute()` 未做输入校验：`execute()` 补充 `validateInput()` 调用，与 `processUserMessage()` 保持一致
- **修复** — `compact()` 中 `ctx.substr(0, 500)` 可能在 UTF-8 多字节字符中间截断：添加 UTF-8 续字节检测循环

### 注释补充

- **ContextManager.h** — `assemble()` 文档扩展为完整 7 步流水线
- **ContextManager.cpp** — 补充 `buildSystemPrompt`、`compact`、`isVectorStoreStale`、`setRetrievalBackend`、`vector_store_dirty_` 的详细中文注释
- **SessionPersistence.h** — `SessionMeta` 所有字段添加 Doxygen 注释；`SessionPersistence` 类补充双文件设计说明
- **SessionPersistence.cpp** — `save()` / `load()` 补充序列化格式和防御式解析说明
- **ToolCallLoop.h** — `run()` 的 `initial_messages` 参数补充完整文档（为什么存在 + 首轮/后续轮次行为差异）
- **Agent.h** — `compactConversation` / `rewindTo` / `saveSessionState` / `loadSessionState` / `maybeAutoCompact` 扩展 docstring
- **Agent.cpp** — `validateInput`（两层防御说明）、`resetSession`（级联注释）、`saveSessionState`/`loadSessionState`（流程注释）
- **IMessageProcessor.cpp** — 同步 compact 步骤补充逃生阀设计原理注释

## [2026-06-20] LLMClientFactory — 实例级线程隔离 + 5 个预存在 Bug 修复

### Phase 4 线程安全：实例级隔离

- **新增** — `src/llm/LLMClientFactory.h/.cpp`：工厂类，封装 `ProviderConfig`，`create()` 返回独立 `LLMClient` 实例
  - 工厂本身不可变（线程安全），可在多线程间共享
  - 每个 `Agent` / `SubAgent` / `AgentOrchestrator` 通过工厂创建自己的 `LLMClient`
  - `AgentOrchestrator` 为每个并行 `SubAgent` 创建独立客户端
  - `SessionManager` 为每个 Session 创建独立 `Agent`（从而独立 `LLMClient`）
- **修改** — `Agent` / `SubAgent` / `AgentOrchestrator` / `SessionManager` / `BackendServer` 全部改用工厂模式
  - `Agent` 持有 `unique_ptr<ILLMClient>` + `LLMClientFactory&`（用于 `useParallelProcessor`）
  - `SubAgent` 新增测试用构造函数 `SubAgent(unique_ptr<ILLMClient>, IToolProvider&)`
  - `ParallelProcessor` → `AgentOrchestrator` 链路全部通过工厂创建独立客户端
- **注释** — 所有相关类的线程安全注释更新（`LLMClient` / `HttpClient` / `Agent` / `SubAgent` / `AgentOrchestrator` / `SessionManager` / `BackendServer`）

### Bug 修复（预存在，本次审查发现并修复）

- **修复** — `AgentOrchestrator::executeParallel()` 节流循环双重消费 `std::future` 导致 `std::future_error` 崩溃
  - 引入 `consumed[]` 追踪已在节流中收集的 future，最终收集循环跳过已消费项
  - 同时添加 `std::this_thread::yield()` 消除节流轮询忙等待
- **修复** — `BackendServer::/api/chat` 同一会话并发请求导致 Agent 数据竞争
  - `Session` 新增 `std::mutex request_mutex`，请求线程调用 `processUserMessage()` 前加锁串行化
- **修复** — `SubAgent::execute()` 超时后 `future.wait()` 无超时限制，HTTP 挂起时无限阻塞
  - 改为 `future.wait_for(config.timeout * 2)`，超时后记录错误并返回（避免调用方永久卡死）
- **修复** — `ParallelProcessor::process()` 静默丢弃流式回调和 `raw_response`
  - 填充 `raw_response.content` / `finish_reason`
  - 调用 `callbacks.on_complete` / `on_error`
  - 添加异常 try-catch

## [2026-06-11] Tauri 桌面 GUI v0.1.0

- **新增** — `gui/` 目录：Tauri v2 + React 19 + TypeScript 桌面应用
  - React 前端（29 个源文件）：ChatPanel / MessageBubble / StreamingText / ChatInputBar / Sidebar / TopBar / AppLayout
  - Tauri Rust 后端（`src-tauri/`）：Sidecar 生命周期管理（启动/健康检查/关闭）、项目路径记忆、文件夹选择对话框
  - Catppuccin Mocha 深色主题，Markdown 流式渲染，可折叠思考链，自动滚底
  - 技术栈: Vite 6 + Tailwind CSS v4 + Zustand + react-markdown + rfd
- **新增** — 后端 API 补充（`src/server/BackendServer.cpp`）：
  - 全局 CORS 中间件（`set_pre_routing_handler`）— 所有路由自动添加跨域头 + OPTIONS 预检
  - `GET /api/project/chapters` — 章节列表（id/title/order/synopsis/status/scenes_count）
  - `GET /api/project/characters` — 角色列表（id/name/role/traits/appearances_count）
- **打包** — NSIS 安装包 7.8MB（含 C++ 后端 22MB + 9 个 MinGW DLL + React 前端 404KB）
- **路径** — 首次启动弹出文件夹选择框，之后自动记住（`%APPDATA%/novelagent/last_project.txt`）
- **环境** — Rust 1.96.0 安装至 `D:\Rust\`，USTC 镜像加速
- **构建** — `npm run tauri:build` 一键产出安装包；`npm run copy:sidecar` 复制 C++ 二进制 + DLL

## [2026-06-10] FTXUI TUI — 类 Claude Code 终端界面

- **新增** — `src/tui/` 模块（5 个组件，纯 C++20 + FTXUI）:
  - `TuiApp` — 主控制器：ScreenInteractive 事件循环、组件树组装、Worker 线程调度
  - `TuiChatPanel` — 聊天面板：流式消息渲染、多角色颜色区分（用户/助手/错误/系统）
  - `TuiInputBar` — 输入栏：命令历史（↑↓）、Enter 提交、占位提示
  - `TuiStatusBar` — 状态栏：模式标签（就绪/思考中/执行工具/错误）、Token 用量、项目信息
  - `TuiSidebar` — 侧边栏：大纲列表 + 角色列表（通过 Project 数据）
- **线程模型** — Worker 线程调用 Agent + `screen.Post()` 桥接 `StreamCallbacks`，UI 不冻结
- **斜杠命令** — `/help` `/exit` `/status` `/clear`（扩展自 CLI CommandParser）
- **CLI 入口** — `novelagent --tui -p ./项目` 启动 FTXUI 终端界面
- **依赖** — FTXUI 6.1.9（MSYS2 `mingw-w64-x86_64-ftxui`），动态链接 3 个 DLL（~2MB）
- **修改** — `NovelAgentApp` 新增 `runTui()`，`main.cpp` 新增 `--tui` 标志
- **修改** — `CMakeLists.txt` 新增 `novelagent_tui` object library
- **新增** — `tests/test_tui.cpp`：12 个 TUI 组件测试（ChatPanel/InputBar/StatusBar/Sidebar）
- 三种模式共存: `novelagent`（REPL）/ `--tui`（FTXUI）/ `backend`（HTTP+SSE）
- 测试统计: **15/15** 全部通过（新增 1 个测试目标）

## [2026-06-10] 清理 — 移除前端代码，回归纯 C++ 后端

- **移除** — Node.js Ink/React TUI 前端（`tui/` 目录，7 个 TypeScript 文件）
- **移除** — TUI-Web 页面（`tui-web/index.html`）
- **移除** — 启动脚本（`start.bat`、`start.sh`）
- **移除** — `main.cpp` 中的 `launchDesktop()` 函数 + `--tui` CLI 选项
- **移除** — `.gitignore` 中 `tui/my_novel/` 条目
- **保留** — C++ 后端 Server（`BackendServer`、`SessionManager`），纯 HTTP+SSE API，前端由外部实现
- 项目回归为纯 C++20 代码库

## [2026-06-10] Phase 6 — 前后端分离 + Node.js Ink TUI

- **C++ 后端 Server** — `src/server/BackendServer.h/.cpp` + `SessionManager.h/.cpp`:
  - HTTP+SSE 服务器（基于 httplib），支持多终端同时连接
  - SessionManager：多会话管理（创建/销毁/空闲清理），线程安全
  - SSE 流式聊天：`set_chunked_content_provider` 实现真正的流式响应
  - API 路由：`/api/chat`(SSE) `/api/session` `/api/execute` `/api/project/status` `/api/project/export` `/api/health`
  - 端口文件机制（`.novelagent/port`）供前端自动发现后端
- **新增** — 4 个 C++ 文件
- 测试统计: **14/14** 全部通过

## [2026-06-10] Phase 5 — 打磨 + 终端 GUI (7步全部完成)

- **5.1** — `AnsiTerminal.h`: 统一 ANSI 工具库（颜色/样式/光标/语义主题）
  - Windows `SetConsoleMode` 自动启用 ANSI 支持
  - 语义化颜色：assistant(绿)/userInput(蓝)/toolCall(灰)/thinking(暗)/error(红)/warning(黄)
- **5.2** — Tab 补全 + 4 个新斜杠命令:
  - `/status` — 项目统计（章节/角色/设定/字数/对话）
  - `/config <key> <value>` — 运行时配置（context_window, max_tool_rounds）
  - `/export` — 导出所有章节为单个 Markdown 文件
  - `/save` — 手动保存项目
  - `/trace` — 执行轨迹查询
  - Tab 补全：输入 `/` 后自动补全命令名
- **5.3** — 错误恢复:
  - main.cpp 最外层 try/catch（全局异常兜底）
  - ReplHandler 自动保存（崩溃前保存项目）
  - 磁盘写入失败的友好提示
- **5.4** — `AgentState.h`: 显式状态机
  - `AgentState` 枚举（Idle/Thinking/AwaitingTool/WaitingUser/Error/Fatal）
  - `StateMachine` 类：状态转换 + 合法性检查 + 日志
  - 状态名中文化（"就绪"/"思考中"/"执行工具"等）
- **5.5** — `ParameterValidator.h/.cpp`: 工具参数 Schema 校验
  - 必填字段检查、类型匹配（string/integer/boolean/array/enum）
  - additionalProperties 检测（记录 warning 不阻断）
  - 校验失败返回结构化 JSON 错误 `{"error":"...","details":[...]}`
  - 集成到 `ToolPipeline::executeOne()` 执行前
- **5.6** — `ExecutionTracer.h/.cpp`: Agent 执行轨迹记录
  - `TraceEntry` 结构（timestamp/step_index/event_type/payload/tokens/duration）
  - `dump()` 保存为 JSONL 格式到 `.novelagent/traces/`
  - `summary()` 汇总统计（总步数/token/LLM调用/工具调用/错误）
  - `recentSummary(n)` 最近 N 步文本摘要
- **终端 GUI** — `TerminalGUI.h/.cpp`: Claude Code CLI 风格界面
  - 语义化颜色主题（角色区分）
  - 状态栏渲染（模式 | 项目 | token 用量）
  - Markdown 渲染（**粗体**/*斜体* → ANSI 转义码）
  - 进度指示器（旋转动画）
  - 命令历史管理
  - 标题/分隔线渲染
- **修改** — StreamDisplay(重写-ANSI主题), ReplHandler(重写-GUI+命令), ToolPipeline(校验), NovelAgentApp(项目传递), main(ANSI+错误恢复)
- **新增** — 11 个文件: AnsiTerminal, TerminalGUI, AgentState, ParameterValidator, ExecutionTracer
- 测试统计: **14/14** 全部通过
- **版本**: v0.3.0

## [2026-06-10] 架构深层重构 — 依赖倒置+策略模式+安全约束 (P0-P3)

- **P0** — `ToolCallLoop`: 提取 Agent/SubAgent 中 ~90 行重复 tool call 循环为独立引擎
  - 支持超时控制、首轮流式/非流式配置、统一错误处理
  - Agent::runToolLoop 和 SubAgent::execute 均委托 ToolCallLoop
- **P0** — `IToolProvider` + `RestrictedToolProvider`: 工具访问安全约束
  - SubAgent 不再持有完整 ToolRegistry&，改为持有 IToolProvider&
  - RestrictedToolProvider 白名单机制在类型系统层面保证安全
  - O(n*m) 过滤优化为 O(n)
- **P1** — `ISynthesisStrategy`: AgentOrchestrator 汇总策略接口
  - LlmSynthesis（LLM汇总）、ConcatSynthesis（简单拼接）、CustomSynthesis（注入函数）
  - AgentOrchestrator::synthesize() 不再硬编码 LLM 调用
- **P1** — `IMessageProcessor`: 消除 Agent 硬编码串行/并行分支
  - SerialProcessor（tool call 循环）、ParallelProcessor（委托 Orchestrator）
  - 新增 PlanThenExecute 模式只需实现接口并注入
- **P1** — `AgentOrchestratorTypes.h`: 分离 SubTask 类型到独立头文件
- **新增** — 10 个文件: ToolCallLoop, IToolProvider, IMessageProcessor, ISynthesisStrategy, AgentOrchestratorTypes
- **修改** — Agent(重写-策略模式), SubAgent(IToolProvider), AgentOrchestrator(ISynthesisStrategy), NovelAgentApp(适配)
- 测试统计: **14/14** 全部通过

## [2026-06-10] Phase 4 架构重构 — 审查问题修复 (P0-P3)

- **P0** — `HttpClient`: 提取共享 HTTP 基础设施（URL解析/认证/重试/错误处理）
  - LLMClient 和 EmbeddingGenerator 均通过组合持有 HttpClient，消除 ~150 行重复代码
  - LLMClient.cpp: 从 363 行精简到 140 行（-61%）
  - EmbeddingGenerator.cpp: 从 262 行精简到 155 行（-41%）
- **P1** — 检索模块抽象接口: `IVectorStore` + `IEmbeddingGenerator`
  - VectorStore 和 EmbeddingGenerator 改为实现纯虚接口
  - 支持 Mock 测试，未来可替换为 sqlite-vec / ONNX 后端
- **P1** — 拆分 ContextManager 上帝类（7→1 职责）:
  - `ConversationSummarizer` — 对话摘要（规则提取+渲染）
  - `ChapterSummaryCache` — 章节摘要缓存 CRUD（通过 IStorageBackend）
  - `DegradationPipeline` — 策略模式降级管线（5个独立策略类）
  - `SessionPersistence` — 会话保存/加载/归档
  - ContextManager 精简为编排器（~150 行），组合 4 个子模块
- **P2** — ContextManager 通过 IStorageBackend 访问存储:
  - `FileStorageBackend` 适配 ProjectIO → IStorageBackend
  - ContextManager 构造函数注入 `IStorageBackend&`，不再直接依赖 ProjectIO
  - 符合 CLAUDE.md 架构规则
- **P2** — 降级策略模式: `IDegradationStrategy` + 5 个具体策略类 + `DegradationPipeline`
  - 新增降级等级只需实现接口并注册，符合开闭原则
- **P3** — `SummaryKeywords` 配置化: 剧情/任务关键词可通过构造函数或 setter 定制
- **新增** — 15 个文件: Http客户端、4个抽象接口、4个拆分类、FileStorageBackend、ContextManagerTypes
- **修改** — ContextManager(重写)、LLMClient(瘦身)、EmbeddingGenerator(瘦身)、NovelAgentApp(适配)
- 测试统计: **14/14** 全部通过

## [2026-06-09] Phase 4 — 上下文管理与语义检索 (9步)

- **新增** — `ContextManager` Phase 4 完整版:
  - 对话历史摘要 `summarizeConversation()`：规则提取角色名/章节引用/剧情点/任务
  - 章节摘要缓存：`.novelagent/summaries.json` 读写，支持按章节 ID 索引
  - 预算分配 `allocateBudget()`：50/30/20 规则（章节/对话/摘要）
  - 多级降级（L1-L5）：截断章节→移除角色详情→移除相邻章节→截断对话→全文压缩
  - 会话持久化 `saveSession()`/`loadSession()`/`archiveSession()`
- **新增** — `VectorStore` (JSON 后端 + 暴力余弦相似度):
  - CRUD: insert/insertBatch/remove/update + 持久化到 JSON 文件
  - 搜索: `search()` Top-K 余弦相似度排序，< 10ms @ 万级向量
  - API 兼容 sqlite-vec（后续仅需替换 .cpp 内部实现）
- **新增** — `EmbeddingGenerator`:
  - 调用 OpenAI 兼容 `/v1/embeddings` API
  - 支持单条/批量嵌入，自动分批（max_batch_size=100）
  - 指数退避重试（3 次）+ 文本截断预处理
- **新增** — `NovelChunker`:
  - 章节切分：优先按 Scene 边界，退化为段落边界（500-2000字/chunk, 15% 重叠）
  - 实体拼接：`chunkCharacter()`/`chunkSetting()`/`chunkWorldRule()` 生成可嵌入文本
- **新增** — `tests/test_retrieval.cpp`：15 个检索模块测试
- **修改** — `tests/test_context_manager.cpp`：扩展至 22 个测试（Phase 4.1-4.4）
- **修改** — `CMakeLists.txt`：新增 `src/retrieval/` 模块到 novelagent_core
- 测试统计: **14/14** (新增 1 个测试目标，test_context_manager 扩增 16 子测试)

## [2026-06-09] Phase 3.5 — 多Agent并行编排 (9步)

- **新增** — `SubAgent`: 独立对话上下文 + 受限工具集(std::async) + 120s超时
- **新增** — `AgentOrchestrator`: 分解→并行→汇总 (max_parallel=4, std::async)
- **新增** — `SubAgentTemplate`: 5个内置模板(chapter-consistency等)
- **新增** — `TemplateManager`: 内置+用户模板CRUD
- **修改** — `ReplHandler`: /parallel + /agent 命令族
- **修改** — `NovelAgentApp`: 集成AgentOrchestrator+TemplateManager
- 测试统计: 12/12

## [2026-06-09] Phase 3.6-3.12 — 剩余工具 + REPL 集成（Phase 3 核心完成）

- **新增** — Setting 工具: `get_setting` / `get_settings` / `update_setting`
- **新增** — WorldRule 工具: `get_world_rule` / `get_world_rules` / `update_world_rule`
- **新增** — Outline 工具: `get_outline`
- **新增** — Project 工具: `get_project_status`
- **新增** — Shell 工具: `run_powershell`（`_popen` 捕获 stdout + exit_code）
- **新增** — `AgentSetup.h`: `registerAllTools()` 一键注册全部 17 个工具
- **新增** — `CommandParser`: 斜杠命令解析（/help /exit /clear /tools /model）
- **新增** — `StreamDisplay`: 流式输出包装（内容/思维链/工具调用/token 统计）
- **新增** — `ReplHandler`（完整版）: REPL 主循环 + 流式显示 + 命令拦截
- **修改** — `main.cpp`: 完整集成 CLI（-p 项目 -e 单次 --provider -v）
- 工具总数: **17 个** (Chapter 5 + Character 4 + Setting 3 + WorldRule 3 + Outline 1 + Project 1 + Shell 1)
- 测试统计: 12/12 全部通过
- CLI 验证: `novelagent -p test -e "..."` --exec 模式端到端跑通

## [2026-06-09] Phase 3.5 — Character 工具（4 个）

- **新增** — `src/agent/tools/CharacterTools.h/.cpp`：4 个角色管理工具类
  - `GetCharacterTool` — 按 ID 查询角色完整档案（利用 Models.h 的 to_json 序列化）
  - `ListCharactersTool` — 列出所有角色摘要（id/name/role/goal）
  - `CreateCharacterTool` — 创建角色（自动生成 char-001 格式 ID，重名检测，补零对齐）
  - `UpdateCharacterTool` — 更新角色字段（指针到成员 map 驱动，支持 16 个 string + 4 个 array 字段）
- **新增** — `tests/test_character_tools.cpp`：6 个测试（创建/查询/列表/更新/错误处理/ToolRegistry）
- 测试统计：12/12 全部通过（新增 1 个测试目标）

## [2026-06-09] 第三轮代码审查修复 — REVIEW_NOTES.md 11 问题

- **修复** — #1 Agent 不再覆盖用户的 `system_prompt_`：ContextManager 产出用局部变量拼接（审查发现的回归 bug）
- **修复** — #2 `truncateMessages()` token 公式统一为 `countMessages()` 循环重算（审查发现的 bug）
- **修复** — #3 `buildSystemPrompt()` 无效章节 fallback 到项目概述（审查发现的 bug）
- **修复** — #4 `truncateMessages()` budget ≤ 0 时返回空列表（审查发现的 edge case）
- **修复** — #6 `CreateChapterTool` 恢复全量保存（审查发现的回归 bug）
- **修复** — #7 工具执行结果 4000 字符截断（防止单条消息超出 token 预算）
- **修复** — #8 删除 `processUserMessage` 中空 `try/catch`（审查发现的死代码）
- **修复** — #9 `ListChaptersTool` 描述与实际行为同步（审查发现的文档不一致）
- **修复** — #11 `ContextAssembly::total_tokens` 注释标注为估算值
- 暂缓 — #5 Project& 生命周期约束（等 Phase 3.5）+ #10 additionalProperties 安全默认
- 影响范围：`Agent.cpp`、`ContextManager.h/.cpp`、`ChapterTools.h/.cpp`
- 测试统计：11/11 全部通过

## [2026-06-09] Step 3.1-3.4 代码审查修复

- **修复** — Agent 集成 ContextManager：`runToolLoop` 每次 LLM 调用前做 token 预算截断（可选，通过 `setContextManager()` 启用）
- **修复** — `ListChaptersTool` 不再逐章读取文件（100 章 = 省 100 次磁盘 I/O），改为只返回元数据
- **修复** — 删除 `countWords()` 重复实现（与 `TokenCounter` 功能重复且算法不一致）
- **修复** — `CreateChapterTool` 只保存 `outline.json` 而非全部 6 个 JSON 文件
- **修复** — Agent 新增 `setContextWindow()` 配置入口
- 测试统计：11/11 全部通过

## [2026-06-08] Phase 3.4 — Chapter 工具（5 个）

- **新增** — `src/agent/tools/ChapterTools.h/.cpp`：5 个章节操作工具类
  - `ReadChapterTool` — 读取章节 Markdown 全文
  - `WriteChapterTool` — 覆写章节内容
  - `CreateChapterTool` — 创建新章节 + 更新 outline + 写入文件
  - `AppendChapterTool` — 读取 → 追加 → 写回
  - `ListChaptersTool` — 列出所有章节 ID/标题/顺序/字数
- **新增** — 每个工具持有 `Project&` 引用，通过 `ProjectIO` 执行磁盘 I/O
- **新增** — `tests/test_chapter_tools.cpp`：7 个集成测试（临时目录 + 真实文件 I/O）
- **修复** — 文件名使用章节 ID（`ch-001.md`）而非标题，避免 Windows 窄字符 API 下 UTF-8 路径问题
- 影响范围：`src/agent/tools/ChapterTools.h/.cpp`、`tests/test_chapter_tools.cpp`
- 测试统计：11/11 全部通过（新增 1 个测试目标）

## [2026-06-08] Phase 3.3 — ContextManager（基础版）

- **新增** — `src/agent/ContextManager.h/.cpp`：上下文管理器，负责 token 预算计算 + 消息截断 + 系统提示词构建
- **新增** — `ContextAssembly` 结构体：截断后消息 + 系统提示词 + 预算统计 + 截断元信息
- **新增** — `calculateBudget()`：80/20 规则（80% 输入 + 20% 输出预留）
- **新增** — `truncateMessages()`：从旧到新移除超出预算的消息，保证最新消息不丢失
- **新增** — `buildSystemPrompt()`：委托 PromptContextBuilder 按章节构建系统提示词
- **新增** — `tests/test_context_manager.cpp`：6 个测试（预算计算、不截断、截断触发、无 Project、有/无章节）
- 影响范围：`src/agent/ContextManager.h`、`src/agent/ContextManager.cpp`、`tests/test_context_manager.cpp`
- 测试统计：10/10 全部通过（新增 1 个测试目标）

## [2026-06-08] Phase 3.2 — Agent 核心循环

- **新增** — `src/agent/Agent.h/.cpp`：核心 Agent 类，实现 `processUserMessage()` 和 `execute()` 两种入口
- **新增** — Tool call 循环：LLM 请求工具 → Agent 执行 → 回传结果 → 再次调用 LLM（最多 10 轮）
- **新增** — 首轮流式 + 后续非流式的混合调用策略（用户看到实时输出，工具循环节省开销）
- **新增** — 对话历史自动管理：用户消息、assistant 回复、tool 结果自动追加到 Conversation
- **新增** — `tests/test_agent.cpp`：5 个 Mock HTTP 测试（简单对话、tool call 循环、execute 模式、对话管理、空输入）
- 影响范围：`src/agent/Agent.h`、`src/agent/Agent.cpp`、`tests/test_agent.cpp`、`CMakeLists.txt`、`tests/CMakeLists.txt`
- 测试统计：9/9 全部通过（新增 1 个测试目标）

## [2026-06-08] Phase 3.1 — ToolRegistry + 内置工具架构

- **新增** — `src/agent/tools/BuiltInTool.h`：工具抽象基类 + `ToolCategory` 枚举（7 个类别）+ `toDefinition()` 转换
- **新增** — `src/utils/SchemaUtils.h`：JSON Schema 构建辅助（`object` / `stringProp` / `integerProp` / `booleanProp` / `stringEnum` 等）
- **新增** — `src/agent/ToolRegistry.h/.cpp`：工具注册中心，支持 `registerTool()`（函数式）和 `registerBuiltInTool()`（类式）两种注册方式
- **新增** — `tests/test_tool_registry.cpp`：8 个测试（函数式/类式注册、执行、ToolDefinition 输出、错误处理、分类查询、SchemaUtils）
- 影响范围：`src/agent/`、`src/utils/SchemaUtils.h`、`CMakeLists.txt`、`tests/CMakeLists.txt`
- 测试统计：8/8 全部通过（新增 1 个测试目标）

## [2026-06-08] 新增 LLM 请求→响应流程图文档

- **新增** — `docs/diagrams/` 目录：存放流程图和架构图
- **新增** — `docs/diagrams/LLM请求响应流程图.md`：Mermaid 格式的完整流程图（含 7 张图）
  - 总览：非流式 vs 流式两条路径对比（flowchart）
  - 详细时序图：从用户输入到 LLMResponse 返回（sequence diagram, 7 个阶段）
  - 组件数据流图：SSE 文本 → StreamChunk → LLMResponse 的类型转换链
  - 错误处理路径：4 层错误检测（配置/HTTP/SSE/完整性）及中文错误映射
  - 数据结构对照表：各阶段数据类型的来源/去向
  - 非流式 vs 流式对比表
- 可在 VS Code 中按 `Ctrl+Shift+V` 直接预览 Mermaid 渲染效果

## [2026-06-08] 编译速度优化 — 对象库消除重复编译

- **新增** — CMake 对象库 `novelagent_lib`：所有业务源码编译为 `.o` 集合，主程序和测试共享
- **修改** — 每个 `.cpp` 从编译 2~4 次降为 1 次（`SSEParser.cpp`: 4×→1×, `LLMClient.cpp`: 3×→1×, `StreamAccumulator.cpp`: 3×→1×）
- **修改** — 测试目标大幅简化：每个测试从 ~10 行（include 路径 + 链接库 + 源文件列表）简化为 ~5 行
- **修改** — 构建生成器从 MSYS Makefiles 切换为 Ninja（自动检测），增量构建更快
- **修改** — `add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT)` 从全局改为 `target_compile_definitions` 精确作用域
- **修改** — MSYS2 DLL PATH 覆盖所有测试目标（对象库 PUBLIC 链接使所有测试都依赖 OpenSSL/spdlog DLL）
- 影响范围：`CMakeLists.txt`、`tests/CMakeLists.txt`
- 测试统计：7/7 全部通过，增量构建 ~12s（修改 1 个 `.cpp`）

## [2026-06-08] Message.h 协议构造代码封装

- **新增** — `Message` 静态工厂方法：`user()`、`system()`、`assistant()`、`toolResult()`，替代冗长的聚合初始化
- **新增** — `Conversation` 类（`src/llm/Conversation.h`）：封装对话历史管理，提供 `addUser()`、`addAssistant()`、`systemPrompt()`、`messages()` 等便捷方法
- **新增** — `tests/test_sse_helpers.h`：SSE 测试数据构造辅助（`sseContentChunk` / `sseFinishChunk` / `sseToolCallChunk`），消除测试中手工拼接 JSON 字符串
- **修改** — `test_llm_client.cpp`：迁移至新 API（工厂方法 + SSE 辅助），删除手工 JSON 拼接代码
- **修改** — `test_deepseek_smoke.cpp`：迁移至 `Message::user()` 工厂方法
- 影响范围：`src/llm/Message.h`、`src/llm/Conversation.h`、`tests/test_sse_helpers.h`、`tests/test_llm_client.cpp`、`tests/test_deepseek_smoke.cpp`
- 测试统计：7/7 全部通过

## [2026-06-08] 流式响应字段封装 — StreamingTypes 分离 + StreamingPipeline

- **新增** — `src/llm/StreamingTypes.h`：从 Message.h 拆分出 `ToolCallDelta`、`UsageInfo`、`StreamChunk` 三个流式中间类型
- **新增** — `src/llm/StreamingPipeline.h`：封装 SSEParser + StreamAccumulator + 回调路由为统一流式管道 facade
- **修改** — `LLMClient::chat()`：管道装配从 ~40 行简化为 ~15 行，使用 `StreamingPipeline`
- **修改** — `SSEParser.h` / `StreamAccumulator.h`：include 改为直接引用 `StreamingTypes.h`
- **修改** — `Message.h`：移除流式类型（~35 行），末尾 `#include "StreamingTypes.h"` 保持向后兼容
- 影响范围：`src/llm/Message.h`、`src/llm/StreamingTypes.h`、`src/llm/StreamingPipeline.h`、`src/llm/SSEParser.h`、`src/llm/StreamAccumulator.h`、`src/llm/LLMClient.cpp`
- 测试统计：7/7 全部通过

## [2026-05-31] 依赖管理迁移 MSYS2 + Phase 2 代码审查修复

- **新增** — MSYS2 pacman 优先 + FetchContent 回退的二级依赖管理（`find_package` → `FetchContent`）
- **新增** — `docs/review/DEFERRED.md` 暂缓问题记录（PCH、Volume/Chapter 字段重叠）
- **修改** — 依赖：nlohmann_json v3.12.0、CLI11 v2.6.2、spdlog v1.17.0（MSYS2 预编译包）
- **修改** — cpp-httplib 启用 OpenSSL 支持（`HTTPLIB_USE_OPENSSL ON`）
- **修改** — `AppConfig::load()` 加载顺序：当前目录 config.json 优先于全局 `~/.novelagent/`
- **修复** — `LLMClient.h` 删除未使用的重复超时常量（`kConnectionTimeout`/`kReadTimeout`）
- **修复** — `TokenCounter::estimateEnglishWords` 修复 `std::isalpha` 对非 ASCII 字符的 UB
- **修复** — `PromptContextBuilder::selectPlotThreads` POV 为空时的回退逻辑改进
- **修复** — 测试运行时 DLL 找不到：MSYS2 动态库路径加入 `ENVIRONMENT_MODIFICATION`
- **修改** — CMake 最低版本升至 3.24
- **修改** — `CPPHTTPLIB_OPENSSL_SUPPORT` 移除重复的 target 级定义，仅保留全局
- **新增** — `OPENSSL_ROOT_DIR` 缓存路径存在性校验（跨机器共享 build 目录时自动清理）
- **新增** — `tests/test_deepseek_smoke.cpp` DeepSeek API 冒烟测试（手动执行，不在 CTest 中）
- **删除** — `docs/review/VOLUME_CHAPTER_FIELD_OVERLAP.md`（内容合并至 DEFERRED.md）
- 影响范围：`CMakeLists.txt`、`cmake/FetchDependencies.cmake`、`src/llm/`、`src/prompt/`、`src/config/`、`tests/`、`docs/review/`

## [2026-05-30] Phase 2 完成 — LLMClient + 测试全覆盖

- **新增** — `LLMClient` 类实现（Step 2.5），支持流式 `chat()` 和非流式 `chatNonStreaming()`
- **新增** — `StreamCallbacks` 回调结构体：on_content/on_reasoning/on_tool_call_start/on_complete/on_error
- **修改** — `Message::to_json` 修复：content 空 + tool_calls 非空 → null（OpenAI API 要求）
- **修改** — `StreamAccumulator` 新增 `completed_` 标志防止 [DONE] 二次触发覆盖 finish_reason
- **新增** — `test_sse_parser.cpp`（Step 2.6）：10 个 SSE 解析测试（token/tool_call/buffer/[DONE]/error）
- **新增** — `test_llm_client.cpp`（Step 2.7）：5 个 Mock HTTP 服务器测试（流式/非流式/401/缺 Key）
- **修改** — `PLAN.md` 更新至 v3.3，Phase 2 标记为已完成
- 测试统计：7 个可执行文件，36+ 测试点，ctest 100% 通过
- 影响范围：`src/llm/`、`tests/`、`CMakeLists.txt`、`PLAN.md`

## [2026-05-29] 流式架构重构 — StreamChunk + StreamAccumulator 职责分离

- **新增** — `Message.h` 中新增 `ToolCallDelta`、`UsageInfo`、`StreamChunk` 三个流式中间类型
- **修改** — `SSEParser` 简化为纯协议解析：`onChunk(StreamChunk)` 单回调替代 `onToken`/`onToolCall`/`onDone` 三回调，移除 `pending_tool_calls_` 和 `flushToolCalls()`
- **新增** — `StreamAccumulator` 类负责跨 chunk 合并（文本拼接 + tool_calls 按 index 累积 + 流结束时产出 `LLMResponse`）
- **修改** — `docs/review/REVIEW_NOTES.md` 清空（问题已移至 RESOLVED.md）
- 数据流：`SSE 文本 → SSEParser → StreamChunk → StreamAccumulator → LLMResponse`
- 影响范围：`src/llm/Message.h`、`src/llm/SSEParser.h`、`src/llm/SSEParser.cpp`、`src/llm/StreamAccumulator.h`、`src/llm/StreamAccumulator.cpp`、`CMakeLists.txt`

## [2026-05-29] 数据模型新增 Volume（卷纲）+ CharacterDevelopment（角色发展记录）+ 静态链接

- **新增** — `Volume` struct（14 字段）：卷级叙事弧线（title/summary/theme/goal/key_events 等），存储在 outline.json 内
- **新增** — `Chapter.volume_id` 字段 + `Outline.volumes[]`，章节可关联到所属卷
- **新增** — `CharacterDevelopment` struct（7 字段）：记录角色在剧情中的变化（外观/性格/能力等），支持按章节过滤和排序
- **新增** — `Character.development[]` 字段，可通过 `generation.exclude_fields = ["development"]` 整体控制
- **修改** — `format_version` 3→4（新增 Volume 向后兼容，旧项目 volumes 默认为空）
- **修改** — `PromptContextBuilder` 集成 Volume 上下文注入 + 角色发展记录过滤（按章节 order）+ 排序（chronological）+ GenerationControl 检查
- **修改** — `cmake/CompilerSettings.cmake` 添加 `-static-libgcc -static-libstdc++ -static`，exe 不再依赖外部 MinGW 运行时 DLL
- **新增** — 测试：`test_models` +4（Volume 往返/默认值/Outline 集成/Chapter.volume_id），+3（CharacterDevelopment 往返/默认值/Character 集成）
- **新增** — 测试：`test_prompt_context` +4（Volume 注入/不匹配告警/无 volume_id/order==0 + orphan 告警 + GenerationControl 排除）
- 影响范围：`src/project/Models.h`、`src/project/ProjectIO.h/.cpp`、`src/prompt/PromptContextBuilder.h/.cpp`、`cmake/CompilerSettings.cmake`、`tests/test_models.cpp`、`tests/test_prompt_context.cpp`

## [2026-05-28] 代码审查问题修复 — Message.h 完善 + SSEParser 流式合并 + 文档同步

- **新增** — `LLMResponse` 扩展 7 个字段：id, created, total_tokens, reasoning_content, cached_tokens, reasoning_tokens, system_fingerprint
- **新增** — `Message.h` 中 ToolCall/Message/ToolDefinition/LLMResponse 全部添加 `to_json`/`from_json`（与 Models.h 风格一致）
- **新增** — `roleToString`/`roleFromString` 辅助函数，MessageRole 枚举与 JSON 字符串互转
- **修改** — `SSEParser` 流式 tool_calls 按 index 累积合并，arguments 增量拼接，遇 finish_reason 触发完整回调
- **修改** — `TokenCounter::countMessages` 补充统计 `tool_call_id` 和 `name` 字段
- **修改** — `docs/PROJECT_ANALYSIS.md` + `docs/MODULES.md` 更新至 Phase 1 完成/Phase 2 进行中状态
- **修改** — `docs/REVIEW_NOTES.md` 修正 3 处不准确描述（#5 attributes 迁移/#9 命名空间/#10 测试列表）
- **删除** — `docs/CHANGELOG.md`（冗余，根目录 CHANGELOG.md 为唯一维护版本）
- 影响范围：`src/llm/Message.h`、`src/llm/SSEParser.h`、`src/llm/SSEParser.cpp`、`src/llm/TokenCounter.cpp`、`docs/`

## [2026-05-27] PLAN.md v3.1 — sqlite-vec 语义检索方案设计 + 常量注释补充

- **新增** — PLAN.md 依赖选择表加入 sqlite-vec（向量存储与 ANN 搜索，FetchContent 编译为静态库）
- **新增** — `src/retrieval/` 模块设计：VectorStore（sqlite-vec 封装）、EmbeddingGenerator（LLM embeddings API）、NovelChunker（场景边界智能切分）
- **新增** — 上下文管理策略新增"语义检索策略"章节：混合检索架构（确定性关联 + 语义检索）、嵌入内容策略表
- **新增** — Phase 4 从 5 步扩展到 9 步（Step 4.6-4.9：VectorStore → EmbeddingGenerator → NovelChunker → 混合检索集成）
- **新增** — 项目文件格式新增 `.novelagent/vectors.db`、技术风险表新增 sqlite-vec MinGW 兼容性风险
- **新增** — 测试计划新增 `test_retrieval.cpp`、CLI 斜杠命令新增 `/index`、`/search`
- **修改** — PLAN.md 版本号 3.0 → 3.1，步骤总数 31 → 35，Phase 4 标题改为"上下文管理与语义检索"
- **修改** — `Models.h` 和 `ProjectIO.cpp` 常量/类型别名补充中文注释（延续上一 commit 的注释全面补充工作）

## [2026-05-18] PLAN.md 同步更新至 v3.0

- **修改** — PLAN.md 全面刷新，标记 Phase 1 为已完成，反映实际超规格实现
- **修改** — 目录结构更新：新增 `prompt/`、`WorldRule`、当前测试文件清单
- **修改** — 数据模型章节新增：10 个 struct 设计说明 + GenerationControl 体系
- **修改** — Phase 4 步骤数减少（PromptContextBuilder 已提前落地）
- **修改** — Agent 工具新增 WorldRule CRUD、Phase 3 工具总数更新到 ~21 个
- **修改** — `.gitignore` 新增 `.cache/` clangd 索引目录

## [2026-05-18] 模型深化 — GenerationControl + Scene + WorldRule + PromptContextBuilder

- **新增** — `GenerationControl` 字段级提示词控制，每个 struct 自带 `generation` 字段
- **新增** — `Scene` 强类型结构体，替代 `vector<string>` 场景列表
- **新增** — `Relationship` 强类型结构体，替代 `map<string,string>` 角色关系
- **新增** — `WorldRule` 结构体 + `world_rules.json` 文件，独立规则建模
- **新增** — `PromptContextBuilder` 模块，按章节智能筛选上下文并渲染 LLM prompt
- **新增** — `shouldUseField()` 统一白名单/黑名单/标签过滤逻辑
- **修改** — 所有核心 struct 字段大幅扩展（Chapter/Character/Setting/PlotThread/Outline/Style/Project）
- **修改** — Setting 移除旧版 `attributes` 字段，相关迁移代码精简
- **修改** — Character 关系从 `map<string,string>` 迁移到 `vector<Relationship>`
- **修改** — `format_version` 提升到 3
- **修改** — `defaultNovelJson()` 改用 Project struct 构造，消除字段重复
- **注意** — 此为开发阶段破坏性变更，旧版项目 JSON 不兼容
- 影响范围：`src/project/Models.h`、`src/project/ProjectIO.cpp`、`src/prompt/`、`CMakeLists.txt`、`tests/`

## [2026-05-18] 数据模型重构 — tags + metadata 扩展

- **新增** — 所有核心 struct（Chapter/Character/Setting/Style/Project）增加 `tags` 和 `metadata` 字段
  - `tags`（`vector<string>`）用于轻量分类标签
  - `metadata`（`map<string, json>`）用于半结构化扩展，容纳未来创作元数据
- **新增** — `getMetadataWithUnknownKeys()` 机制，未知 JSON 字段自动收入 metadata
- **新增** — Setting 旧版 `attributes` → metadata 兼容迁移
- **新增** — `format_version` 从 1 提升到 2，`migrateProject()` 自动升级旧项目
- **新增** — `JsonUtils::getObjectOrEmpty()` 辅助函数
- **修改** — 手写 `to_json`/`from_json` 替代 `NLOHMANN_DEFINE_TYPE_INTRUSIVE`
- **修改** — utils 文件补充中文注释
- **修改** — 测试用例适配新字段，新增 `test_legacy_metadata_capture` 和 `test_legacy_load_migration`
- 影响范围：`src/project/Models.h`、`src/project/ProjectIO.cpp`、`src/utils/`、`tests/`

## [2026-05-17] 注释汉化

- **修改** — 所有源码注释从英文翻译为中文，符合项目注释语言规范
- **新增** — CLAUDE.md 项目指引文件
- 影响范围：`src/`、`tests/`、`CMakeLists.txt`、`.claude/settings.json`
