# 「墨染书房」界面改版设计

日期：2026-07-26
状态：已获用户确认
范围：Qt/QML GUI（`src/novelagent_qt/`）+ 少量 C++ 桥接层与回调扩展

## 背景与目标

当前 GUI 存在两类问题：

- 视觉：四栏底色几乎相同（全靠 1px 分割线区分）、图标为 emoji/字符、空状态简陋、消息气泡排版一般。
- 可用性：工具调用状态不显示、reasoning 被丢弃、消息不可复制、右栏无章节列表、多个按钮为空壳。

目标：换用「墨染书房」暖墨主题 + 三栏布局，补齐工具调用展示、思考过程折叠、消息复制、章节列表四项可用性能力。

用户明确排除（本次不做）：设置面板功能、项目新建/切换实际功能、会话历史持久化列表。

## 1. 主题层（Theme.qml）

替换为暖墨配色，背景 token 按栏位语义化：

| Token | 值 | 用途 |
|---|---|---|
| bgSidebar | `#141210` | 左侧栏（最深） |
| bgChat | `#201c17` | 对话区（最浅，视觉焦点） |
| bgReader | `#181511` | 阅读区 |
| bgElevated | `#2a251d` | 输入框、悬浮卡片 |
| bgHover | `#302a20` | hover 高亮 |
| textPrimary | `#e8e2d5` | 正文（宣纸色） |
| textSecondary | `#9a9184` | 次级文字 |
| textFaint | `#6b6355` | 弱化文字 |
| accent | `#c9553e` | 朱砂：按钮、选中、用户标识 |
| accentSoft | `#8c3f2e` | 用户消息气泡底 |
| agentTint | `#a3b48a` | 青竹：Agent 标识、工具卡片 |
| warning | `#d4a373` | 琥珀警示 |
| danger | `#c0392b` | 错误/取消 |
| divider | `#2e2921` | 分割线 |

保留原有字号/间距/圆角/动画 token；字体不变（Noto Serif SC + Microsoft YaHei UI），衬线字体用于标题、消息正文、阅读区。旧 token 名（bgDeep/bgPanel）删除，所有引用处同步更新。

## 2. 布局：四栏 → 三栏

- 删除 `ProjectPanel.qml` 与 `SessionPanel.qml`，新建 `SidebarPanel.qml`（preferred 240px，min 200 / max 320）：
  - 顶部：「墨染」标题（衬线加粗）＋当前项目卡片（显示 `bridge.projectName`，带边框卡片样式，暂无点击功能，仅视觉入口）。
  - 中部：「会话」小节标题 + 会话列表（当前会话高亮）＋「＋ 新建会话」（接 `bridge.newSession()`）。
  - 底部：设置齿轮，悬停 ToolTip「设置功能开发中」。
- `MainWindow.qml` SplitView 改为三栏：SidebarPanel / AgentPanel（fill）/ ReaderPanel（preferred 420px）。
- 标题栏窗口按钮 restyle：✕ 悬停底色变朱砂红、文字变白；─ □ 悬停为 bgHover。

## 3. 对话区增强（AgentPanel / ChatBubble）

### 3.1 工具调用卡片
- C++ 侧：`llm::StreamCallbacks` 现有 `on_tool_call_start`（无参数、每响应仅触发一次）保持不变；在 `llm::StreamCallbacks` 中新增：
  - `std::function<void(const std::string& tool_name)> on_tool_start;`（工具执行前触发）
  - `std::function<void(const std::string& tool_name, bool ok)> on_tool_finish;`（工具执行后触发）
  - 由 `CoreLoop::runImpl` 在执行每个 tool_call 前后触发；StreamCallbacks 已沿 QmlBridge → Agent::process → CoreLoop 既有链路传递，无需改 Agent 公共 API（比早期设想的 CoreLoopHooks 方案更简）。
- `QmlBridge`：`toolCallStarted(QString toolName)` 改为携带实名；新增信号 `toolCallFinished(QString toolName, bool ok)`；经 `QMetaObject::invokeMethod` 队列转发。
- QML 侧：chatModel 条目增加 `type` 字段（`"message"` / `"tool"`）。收到 `toolCallStarted` 时 append tool 条目（`⚙ 工具名 · 执行中…`，青竹色小卡片，居左、窄条样式）；收到 `toolCallFinished` 后更新为 `✓ 工具名`（失败为 `✕ 工具名`，danger 色）。ChatBubble delegate 按 `type` 分发到气泡或工具卡片两种子组件（新建 `ToolCallCard.qml`）。

### 3.2 思考过程折叠
- chatModel 的 assistant 条目增加 `reasoning` 字段；`onReasoningReceived` 累积写入当前 streaming 条目。
- 气泡上方渲染「💭 思考过程 ▸」折叠条（仅当 reasoning 非空），默认收起；展开后为 textFaint 小字、左边框竖线样式。折叠状态存于 delegate（`expanded` 属性），不持久化。

### 3.3 消息复制
- 气泡 hover 时右下角浮现「复制」小按钮；点击复制原始 `content`（QML 无剪贴板 API，采用隐藏 `TextEdit` + `selectAll()` + `copy()` 方案，封装于 ChatBubble 内）；复制后按钮短暂显示「已复制」。

### 3.4 输入区
- 发送按钮左侧加灰色提示文字：`Enter 发送 · Shift+Enter 换行`（sizeCaption、textFaint）。
- 现有 Enter 发送逻辑不变（`!event.modifiers` 已放行 Shift+Enter 换行）。

### 3.5 空状态
- chatModel 为空时居中显示：「墨染」大号衬线字 + 副标题一行 + 3 个建议卡片（bgElevated 圆角卡片，hover 高亮）：
  - 「开始一部新小说」→ 点击填入输入框：`我想开始一部新小说，请帮我构思大纲、角色和世界观`
  - 「创作新章节」→ `根据现有大纲和设定，继续写下一章`
  - 「构建世界观」→ `帮我完善这部小说的世界观设定`
- 点击仅填充 `inputField.text` 并聚焦，不自动发送。

## 4. 阅读区（ReaderPanel）+ 章节桥接

- `QmlBridge` 新增：
  - `Q_INVOKABLE QVariantList chapterList();` — 从 `project_->outline` 遍历 Chapter，返回 `{id, title, order, wordCount}`（wordCount 从章节正文文件长度或缓存获取；获取不到则 0）。
  - `Q_INVOKABLE QString loadChapter(const QString& id);` — 读取章节正文（沿用 ProjectIO/FileStorageBackend 现有读取路径）；失败返回空串并 `errorOccurred`。
  - 新增信号 `chaptersChanged()`：`refreshProject()` 与响应完成（responseComplete 转发块内）后发射，保证仅在主线程发射。
- `ReaderPanel.qml`：
  - 标题栏改为「章节下拉选择器」：ComboBox 风格自绘（当前章节标题 + ▾），点击展开章节列表（Popup，bgElevated），选中后调用 `loadChapter` 并显示。
  - 无章节时显示「暂无章节」占位；`chaptersChanged` 时刷新列表并保持当前选中（若被删则回到占位）。
  - 字数统计移至底部右下角（textFaint）；正文维持衬线、1.9 行高、Markdown 渲染。

## 5. 状态栏（StatusBar.qml）

- 上下文百分比改为迷你进度条（宽 60px、高 4px、圆角；>80% 填充色变 warning），旁边保留百分比数字。
- 模型/Provider 合并为一个 Label：`provider · model`。
- 其余（状态点、Tokens）仅随主题换色。

## 6. 错误处理

- `loadChapter` 读取失败：返回空串 + `errorOccurred(message)`，阅读区显示占位文案，不崩溃。
- `chapterList` 在项目未打开时返回空列表。
- 工具回调在 HTTP/工作线程触发，一律经 QueuedConnection 转发主线程（沿用现有模式）。
- `toolCallFinished` 若因取消未触发：`responseComplete`/`errorOccurred` 时将所有仍处于「执行中」的 tool 条目置为终态（✕）。

## 7. 验证方式

- 构建：现有 CMake preset 构建 GUI target，确保零警告新增。
- C++：若 `chapterList`/`loadChapter` 的数据读取逻辑可下沉为不依赖 Qt 的自由函数，则补充到现有 tests（如 `test_project_io.cpp` 风格）；QmlBridge 信号层不做单测。
- 手动核对清单：三栏布局与配色、工具卡片状态流转、reasoning 折叠、消息复制、空状态建议卡片、章节下拉与正文加载、状态栏进度条、窗口按钮 hover。

## 假设

- 章节正文以文件形式存储于项目目录（`examples/test-proj/chapters/` 结构），可由 id/order 定位；实现时以 ProjectIO 实际接口为准。
- reasoning 内容仅 DeepSeek 等支持 `reasoning_content` 的模型会产生；无 reasoning 时 UI 不显示折叠条，属正常路径。
