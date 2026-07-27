# Bootstrap.h 瘦身方案 — 将启动配置迁移到 QML 前端

> 生成日期：2026-07-27
> 状态：**📋 待定 — 方案已设计，尚未执行**
> 关注方向：激进方案（Bootstrap.h 瘦成骨架，所有配置由 QML 前端接管）

---

## 一、背景

CLI 层移除后，`Bootstrap.h` 仍然承担着多项启动配置职责：
- 命令行参数解析（`-p`, `--provider`, `-v`）
- 配置加载与 Provider 校验
- 项目打开/创建

这些功能在 QML 前端已有图形界面的情况下，应该逐步迁移到 QML 中完成，实现"配置一次，永久生效"的体验。

---

## 二、当前 Bootstrap.h 的职责分析

| # | 职责 | 当前实现 | 能否迁移到 QML | 备注 |
|---|------|----------|---------------|------|
| 1 | 解析 CLI 参数 | CLI11 解析 `-p`/`--provider`/`-v` | ✅ 可移除 | 由 QML 设置面板替代 |
| 2 | 加载 `AppConfig` | 读取 `~/.novelagent/config.json` | ⚠️ 需 QML 间接调用 | C++ 文件 I/O，QML 需通过 QmlBridge 调用 |
| 3 | 环境变量覆盖 API Key | `getenv("DEEPSEEK_API_KEY")` 等 | ✅ 可保留或移入 QmlBridge | CI/CD 场景仍有价值 |
| 4 | 校验 provider + API Key | 检查是否存在、是否为空、是否占位符 | ✅ 可改为 QML 弹窗提示 | 不阻塞启动，QML 里引导配置 |
| 5 | 打开/创建项目 | `ProjectManager::openOrCreate()` | ✅ QML 调用 | 需新增 QmlBridge 方法 |
| 6 | 构造 `NovelAgentApp` | `make_unique<NovelAgentApp>(provider, project)` | ❌ 必需保留 | 可改为延迟初始化模式 |
| 7 | 注册 SIGINT 处理 | `signal(SIGINT, sigint_handler)` | ❌ 与 UI 无关，保留 | 可移到 `main_gui.cpp` |

---

## 三、"一次配置，永久生效"设计原则

核心思想：用户只需在首次启动时完成配置，后续启动自动恢复上次的状态。

### 3.1 需要持久化的配置项

| 配置项 | 存储位置 | 说明 |
|--------|----------|------|
| **上次打开的项目路径** | `~/.novelagent/config.json` 或项目级记录 | 启动时自动加载，跳过项目选择 |
| **默认 Provider** | `~/.novelagent/config.json` | 已存在 `default_provider` 字段 |
| **每个 Provider 的 API Key** | `~/.novelagent/config.json` | 已存在 `providers[].api_key` |
| **模型参数**（temperature, model 等） | `~/.novelagent/config.json` | 已存在 |
| **调试日志开关** | `~/.novelagent/config.json` | 新增字段 `verbose` |
| **窗口状态**（位置、大小、布局比例） | `~/.novelagent/config.json` 或 Qt settings | 新增 |

### 3.2 启动流程（理想态）

```
启动 novelagent_gui.exe
    │
    ▼
Bootstrap::run() 极简初始化
    │   ├── 构造 NovelAgentApp（无 provider，无 project）
    │   └── 注册 SIGINT
    ▼
QML 窗口打开
    │
    ├── 读取 config.json（通过 QmlBridge）
    │   ├── 如有上次打开的项目路径 → 自动加载
    │   ├── 如有默认 Provider + API Key → 自动配置
    │   └── 全部就绪 → 直接进入工作状态
    │
    └── 如果缺少必要配置（首次启动）
        ├── 显示"欢迎/初次设置"向导
        │   ├── 选择 Provider 并输入 API Key
        │   └── 创建或打开项目
        └── 配置完成后进入工作状态
```

### 3.3 设置界面布局建议

```
墨染设置
├── Provider 选项卡
│   ├── 默认 Provider 下拉框（deepseek / kimi / claude）
│   ├── API Key 输入框（密码模式）
│   ├── 模型名输入框
│   ├── Temperature 滑块
│   └── 其他高级参数
│
├── 项目选项卡
│   ├── 当前项目: xxx
│   ├── [打开项目] 按钮 → 文件对话框
│   ├── [新建项目] 按钮 → 输入名称和路径
│   └── "启动时自动打开上次项目" 开关
│
├── 调试选项卡
│   ├── "启用调试日志" 开关
│   └── "显示 Token 用量" 开关
│
└── [保存] 按钮
```

---

## 四、激进方案：Bootstrap.h 瘦身计划

### 4.1 目标形态

```cpp
// Bootstrap.h — 瘦身后只做"无法不做"的事
inline Context run(int argc, char** argv) {
    Context ctx;
    try {
        // 构造一个空的 NovelAgentApp，无 provider 无 project
        // NovelAgentApp 需要支持"延迟初始化"模式
        ctx.app = std::make_unique<NovelAgentApp>();
        g_cancel_flag = ctx.app->agent().cancelFlag();
        signal(SIGINT, sigint_handler);
    } catch (const std::exception& e) {
        std::cerr << "[错误] 启动失败: " << e.what() << "\n";
        ctx.exitCode = 1;
    }
    return ctx;
}
```

Bootstrap.h 不再需要：
- CLI11 参数解析
- `<CLI/CLI.hpp>` include（可移除该依赖）
- `AppConfig` 加载和校验
- `ProjectManager` 调用
- `argToUtf8` 编码转换（没有 CLI 参数需要转了）
- `AnsiTerminal`（已移除）

Bootstrap.h 只保留：
- `NovelAgentApp` 的 include 和构造
- `g_cancel_flag` + `sigint_handler`
- 最基本的异常捕获和错误输出

### 4.2 NovelAgentApp 需要的变化

当前构造函数签名：
```cpp
NovelAgentApp(const ProviderConfig& provider,
              std::shared_ptr<Project> project,
              std::vector<std::string> disabledTools = {});
```

需要支持延迟初始化，有两种方案：

**方案 A：默认参数构造**
```cpp
NovelAgentApp();  // 无 provider 无 project，内部创建空 LLMClientFactory
void initialize(const ProviderConfig& provider,
                std::shared_ptr<Project> project);
```

**方案 B：全 optional 参数**
```cpp
NovelAgentApp(std::optional<ProviderConfig> provider = std::nullopt,
              std::shared_ptr<Project> project = nullptr);
```

推荐方案 A，更明确地区分"构造"和"初始化"两个阶段。

### 4.3 QmlBridge 需要新增的方法

```cpp
// ── 延迟初始化 ──
Q_INVOKABLE bool initialize(const QString& providerName);
// 加载指定 provider 并初始化 Agent，返回是否成功

Q_INVOKABLE void setApiKey(const QString& provider, const QString& key);
// 设置某个 provider 的 API Key 并保存到 config

Q_INVOKABLE QStringList listProviders() const;
// 返回 config 中已配置的 provider 名称列表

Q_INVOKABLE bool hasApiKey(const QString& provider) const;
// 检查某个 provider 是否已配置 API Key

// ── 项目操作 ──
Q_INVOKABLE QString createProject(const QString& path, const QString& title);
// 创建新项目，返回项目名称；失败返回空串

Q_INVOKABLE QString openProject(const QString& path);
// 打开已有项目，返回项目名称；失败返回空串

Q_INVOKABLE QString lastProjectPath() const;
// 返回上次打开的项目路径（从 config 读取）

// ── 配置持久化 ──
Q_INVOKABLE void saveSetting(const QString& key, const QVariant& value);
Q_INVOKABLE QVariant loadSetting(const QString& key) const;
// 通用键值对持久化接口

// ── 调试 ──
Q_INVOKABLE void setDebugLogging(bool enabled);
```

### 4.4 QML 需要新增的 UI 组件

| 组件 | 说明 | 优先级 |
|------|------|--------|
| `SettingsDialog.qml` | 设置对话框，含多选项卡 | P0 |
| `WelcomeWizard.qml` | 首次启动向导（欢迎 → 选 Provider → 创建/打开项目） | P0 |
| `ProjectPicker.qml` | 打开/新建项目界面 | P0 |
| ~/.novelagent 中新增字段 `last_project_path` | 记录上次打开的项目 | P0 |
| ~/.novelagent 中新增字段 `verbose` | 调试日志开关持久化 | P1 |
| Qt window settings | 窗口位置/大小持久化 | P2 |

---

## 五、执行步骤

### 阶段一：QmlBridge 增强 + 配置持久化（优先级 P0）

```
1. QmlBridge 新增 initialize() / listProviders() / setApiKey() 等方法
2. QmlBridge 新增 createProject() / openProject() / lastProjectPath()
3. AppConfig 新增 last_project_path 和 verbose 字段
4. 实现在 QmlBridge 中通过 AppConfig 读写配置
```

### 阶段二：QML 设置 UI（优先级 P0）

```
5. 新建 SettingsDialog.qml（Provider 设置页 + 项目设置页）
6. 新建 WelcomeWizard.qml（首次启动引导）
7. 新建 ProjectPicker.qml（项目选择器）
8. SidebarPanel 增加"设置"入口
9. StatusBar 增加 Provider 切换交互
```

### 阶段三：NovelAgentApp 延迟初始化（优先级 P0）

```
10. NovelAgentApp 新增默认构造函数
11. NovelAgentApp 新增 initialize(provider, project) 方法
12. setupAgent() 拆分为可多次调用的模式
```

### 阶段四：Bootstrap.h 瘦身（优先级 P0）

```
13. 移除 CLI11 参数解析
14. 移除 AppConfig 加载
15. 移除 ProjectManager 调用
16. 移除 argToUtf8
17. 移除 CLI/CLI.hpp 依赖（如果不再需要）
18. 简化异常处理
```

### 阶段五：补充细节（优先级 P1-P2）

```
19. 窗口状态持久化（位置、大小、SplitView 比例）
20. 自动加载上次项目
21. 启动时检测上次项目是否仍有效
```

---

## 六、依赖关系图

```
main_gui.cpp
    │
    ├── Bootstrap.h (瘦身后)
    │       └── NovelAgentApp (延迟初始化)
    │
    └── qtui::runQmlApp()
            │
            └── QmlBridge (增强版)
                    ├── AppConfig (读写持久化配置)
                    ├── ProjectManager (创建/打开项目)
                    └── NovelAgentApp::initialize()
```

Bootstrap.h 不再直接依赖 `AppConfig` 和 `ProjectManager`，这些依赖转移到 `QmlBridge` 中。

---

## 七、风险与注意事项

| 风险 | 等级 | 缓解措施 |
|------|------|----------|
| NovelAgentApp 延迟初始化涉及大量内部状态重组 | 高 | 先梳理 setupAgent() 的依赖链，确保可重入 |
| QmlBridge 新增方法涉及线程安全 | 中 | 初始化逻辑在 Qt 主线程执行，不与 worker_ 竞争 |
| 启动时无 provider 导致部分功能不可用 | 中 | Agent 构造时使用占位 provider，初始化前拦截消息发送 |
| 用户从 CLI 迁移到 GUI 的配置兼容 | 低 | 配置格式不变，只是增加新字段 |
| CLI11 依赖移除后可执行文件体积减小 | 低 | 但 CLI11 是 header-only，体积影响有限 |

---

## 八、决策

| 因素 | 评价 |
|------|------|
| 用户体验提升 | ✅ 配置一次，永久生效 |
| 代码简洁性提升 | ✅ Bootstrap.h 从 ~200 行减到 ~30 行 |
| 降低 CLI11 依赖 | ✅ 如果不再需要 CLI 参数，可移除 |
| QML 新增代码量 | ⚠️ 中到高（多个新组件 + QmlBridge 方法） |
| NovelAgentApp 重构 | ⚠️ 需要支持延迟初始化，涉及内部状态重组 |

> **当前建议**：确认方向后，从阶段一开始逐步执行。阶段一/二（QmlBridge + QML UI）可与阶段三（NovelAgentApp 重构）并行开发。
