# GUI 内置项目创建与模型设置 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将项目创建/打开、Provider 与 API Key 配置、调试开关全部内置到 QML 前端（首启向导 + 设置对话框），Bootstrap.h 瘦身为纯 SIGINT 骨架，实现"配置一次，永久生效"。

**Architecture:** 采用**整体重建模式（方案 C）**而非原评审文档的"方案 A 延迟初始化"：`QmlBridge` 升级为 `NovelAgentApp` 的拥有者（`std::unique_ptr`），切换 Provider 或项目时销毁旧实例、用现有构造函数重建新实例。`NovelAgentApp` / `Agent` / `setupAgent()` **一行不改**。配置读写由 `QmlBridge` 直接驱动 `AppConfig`（新增 `last_project_path` / `verbose` 字段与来源回写）。

**Tech Stack:** C++20 / Qt 6.8 QML (Quick, QuickControls2, QtQuick.Dialogs) / nlohmann-json / CMake + Ninja (preset `default`)

---

## 零、为什么不用原文档的"方案 A（延迟初始化）"

`docs/review/BOOTSTRAP_SLIM_PLAN_2026-07-27.md` §4.2 推荐给 `NovelAgentApp` 加默认构造 + `initialize()`。调研代码后否决，理由：

1. `Agent` 构造函数（`src/agent/core/Agent.cpp:39-40`）在初始化列表里就执行 `client_(factory.create())`，且以引用持有 `factory_/registry_/memory_`。无 provider 时无法构造合法 factory，"占位 provider"会让半初始化状态泄漏到运行期。
2. `NovelAgentApp::setupAgent()` 的 `BuiltInTool::registerAllTo()` 是一次性注册，重复调用会向 `ToolRegistry` 重复注册工具；改成可重入需要动 registry 清空逻辑、ProgressiveToolProvider 存根缓存等一串内部状态（原文档自己也标了"高风险"）。
3. 整体重建的代价仅是"切换时会话记忆清空"——而切换项目/模型本来就应当开新会话，语义上完全合理。

**结论：`NovelAgentApp(const ProviderConfig&, shared_ptr<Project>)` 构造函数保持不变，由 QmlBridge 在需要时整体重建。**

---

## 文件结构总览

| 文件 | 操作 | 职责 |
|------|------|------|
| `src/config/AppConfig.h` / `.cpp` | 修改 | 新增 `last_project_path`、`verbose`、`source_path`（运行时）、`save()` 无参回写、`defaultPath()`、`ensureDefaultProviders()` |
| `tests/test_app_config.cpp` | 修改 | 新字段往返测试 + 默认 Provider 补齐测试 |
| `src/novelagent_qt/QmlBridge.h` / `.cpp` | 修改 | 拥有 `unique_ptr<NovelAgentApp>`；新增 provider/项目/初始化/持久化 Q_INVOKABLE；worker 线程 detach→join 修复 |
| `src/novelagent_qt/QmlApp.h` / `.cpp` | 修改 | `runQmlApp(argc, argv)` 不再接收 `NovelAgentApp&` |
| `src/Bootstrap.h` | 重写 | 只留 SIGINT（约 25 行） |
| `src/main_gui.cpp` | 修改 | `installSigint()` + `runQmlApp()` |
| `src/novelagent_qt/qml/SettingsDialog.qml` | 新建 | 设置对话框（Provider / 项目 / 调试 三选项卡） |
| `src/novelagent_qt/qml/WelcomeWizard.qml` | 新建 | 首启向导（欢迎 → Provider → 项目） |
| `src/novelagent_qt/qml/MainWindow.qml` | 修改 | 装配对话框/向导 + `tryAutoStart()` + 窗口状态持久化 |
| `src/novelagent_qt/qml/SidebarPanel.qml` | 修改 | 设置齿轮激活，发 `settingsRequested` 信号 |
| `src/novelagent_qt/qml/StatusBar.qml` | 修改 | Provider·模型区域可点击打开设置 |
| `src/novelagent_qt/qml/AgentPanel.qml` | 修改 | 未初始化时禁用发送 |
| `CMakeLists.txt` | 修改 | QML 文件注册 ×2；`COMMON_LIBS` 移除 `CLI11::CLI11` |
| `cmake/FetchDependencies.cmake` | 修改 | 移除 CLI11 块 |

统一构建/测试命令（Windows + pwsh）：
- 构建：`cmake --build build --target novelagent_gui`
- 单测：`cmake --build build --target test_app_config; ctest --test-dir build -R test_app_config --output-on-failure`

---

### Task 1: AppConfig 新增 GUI 持久化字段

**Files:**
- Modify: `src/config/AppConfig.h`
- Modify: `src/config/AppConfig.cpp`
- Test: `tests/test_app_config.cpp`

- [ ] **Step 1.1: 写失败测试**

在 `tests/test_app_config.cpp` 的现有测试函数后（`main` 之前）追加两个测试，并在 `main` 中调用：

```cpp
void test_gui_fields_roundtrip() {
    TEST("GUI 字段 last_project_path / verbose 保存后可重载");
    cleanup();
    AppConfig cfg;
    cfg.default_provider = "deepseek";
    cfg.last_project_path = "D:/novels/my-book";
    cfg.verbose = true;
    std::string path = kTestDir + "/config.json";
    cfg.save(path);

    AppConfig loaded = AppConfig::loadFromFile(path);
    CHECK(loaded.last_project_path == "D:/novels/my-book");
    CHECK(loaded.verbose == true);
    // 旧配置没有这两个字段时应取默认值
    utils::file::writeText(path, R"({"default_provider":"deepseek","providers":{}})");
    AppConfig legacy = AppConfig::loadFromFile(path);
    CHECK(legacy.last_project_path.empty());
    CHECK(legacy.verbose == false);
    cleanup();
    PASS();
}

void test_ensure_default_providers() {
    TEST("ensureDefaultProviders 补齐缺失模板且不覆盖已有配置");
    AppConfig cfg;
    ProviderConfig mine;
    mine.name = "deepseek";
    mine.api_key = "sk-real";
    mine.model = "deepseek-v4-flash";
    cfg.providers["deepseek"] = mine;

    cfg.ensureDefaultProviders();
    CHECK(cfg.providers.size() == 3);                       // deepseek + kimi + claude
    CHECK(cfg.providers["deepseek"].api_key == "sk-real");  // 已有的不被覆盖
    CHECK(cfg.providers["deepseek"].model == "deepseek-v4-flash");
    CHECK(cfg.providers["kimi"].base_url == "https://api.moonshot.cn/v1");
    CHECK(cfg.providers["claude"].base_url == "https://api.anthropic.com");
    CHECK(cfg.providers["kimi"].api_key.empty());           // 模板不带 key
    PASS();
}
```

并在 `main()` 的测试调用列表中追加：

```cpp
    test_gui_fields_roundtrip();
    test_ensure_default_providers();
```

- [ ] **Step 1.2: 运行确认编译失败**

Run: `cmake --build build --target test_app_config`
Expected: 编译错误 `'last_project_path' is not a member of 'AppConfig'`

- [ ] **Step 1.3: 实现 AppConfig 扩展**

`src/config/AppConfig.h` — 在 `struct AppConfig` 内修改（`providers` 成员之后）：

```cpp
struct AppConfig {
    std::string default_provider = "deepseek";
    std::map<std::string, ProviderConfig> providers;

    // ── GUI 持久化字段（阶段一新增）──
    std::string last_project_path;  // 上次打开的项目目录，启动时自动恢复
    bool verbose = false;           // 调试日志开关

    // 运行时记录本配置的加载来源文件，不参与序列化。
    // save() 无参版本回写到该路径，避免"从 A 加载却存到 B"。
    std::string source_path;

    // 从默认位置 ~/.novelagent/config.json 加载配置。
    static AppConfig load();

    // 从指定路径加载配置。
    static AppConfig loadFromFile(const std::string& path);

    // 保存到指定路径，必要时自动创建父目录。
    void save(const std::string& path) const;

    // 回写到加载来源（source_path）；从未落盘过则写 defaultPath()。
    void save() const;

    // 全局配置文件路径：~/.novelagent/config.json
    static std::string defaultPath();

    // 为 deepseek / kimi / claude 补齐默认模板（base_url + model），
    // 已存在的 provider 不做任何修改。首次启动 GUI 向导依赖此方法。
    void ensureDefaultProviders();

    // 如果找不到对应 provider，则返回 nullptr。
    const ProviderConfig* getProvider(const std::string& name) const;
    const ProviderConfig* getDefaultProvider() const;

    // 便捷方法：为某个 provider 设置 API Key，不存在时自动创建条目。
    void setApiKey(const std::string& provider, const std::string& key);
    void addProvider(const std::string& name, const ProviderConfig& config);

    static constexpr const char* kDefaultConfigFile = "config.json";
};
```

`src/config/AppConfig.cpp` — 对应实现：

1. `load()` 的两个成功分支已经走 `loadFromFile`，无需改动；`loadFromFile` 在 `return config;` 前补 `config.source_path = path;`，并在解析段补：

```cpp
        config.last_project_path = utils::json::getOrDefault(j, "last_project_path", std::string{});
        config.verbose           = utils::json::getOrDefault(j, "verbose", false);
```

2. `save(path)` 的 json 组装处补：

```cpp
    j["last_project_path"] = last_project_path;
    j["verbose"] = verbose;
```

3. 文件末尾新增三个方法：

```cpp
std::string AppConfig::defaultPath() {
    return utils::file::joinPath(utils::file::configDir(), kDefaultConfigFile);
}

void AppConfig::save() const {
    save(source_path.empty() ? defaultPath() : source_path);
}

void AppConfig::ensureDefaultProviders() {
    auto ensure = [this](const std::string& name, const std::string& url,
                         const std::string& model) {
        if (providers.count(name)) return;
        ProviderConfig p;
        p.name = name;
        p.base_url = url;
        p.model = model;
        providers[name] = p;
    };
    ensure("deepseek", "https://api.deepseek.com", "deepseek-v4-flash");
    ensure("kimi", "https://api.moonshot.cn/v1", "kimi-k2-turbo-preview");
    ensure("claude", "https://api.anthropic.com", "claude-sonnet-4-20250514");
}
```

注意：`save()` 序列化时**不写** `source_path`。

- [ ] **Step 1.4: 运行测试确认通过**

Run: `cmake --build build --target test_app_config; ctest --test-dir build -R test_app_config --output-on-failure`
Expected: 全部 PASSED（含旧有 D2 迁移测试不回归）

- [ ] **Step 1.5: 提交**

```bash
git add src/config/AppConfig.h src/config/AppConfig.cpp tests/test_app_config.cpp
git commit -m "feat(config): AppConfig 新增 last_project_path/verbose 字段与来源回写"
```

---

### Task 2: QmlBridge 生命周期重构（拥有 NovelAgentApp + 整体重建）

**Files:**
- Modify: `src/novelagent_qt/QmlBridge.h`
- Modify: `src/novelagent_qt/QmlBridge.cpp`

本任务只做"所有权翻转 + 线程修复 + 空指针防护"，不加新的 QML 功能（Task 3/4 加）。改完后 GUI 行为与现在等价（由 Task 5 的 QmlApp 改造衔接）。

- [ ] **Step 2.1: 重写 QmlBridge.h**

替换 `src/novelagent_qt/QmlBridge.h` 全文为：

```cpp
#pragma once

// QmlBridge — C++ ↔ QML 单一桥接对象，同时是 NovelAgentApp 的拥有者。
//
// 生命周期模型（整体重建模式）：
//   - 构造时仅加载 AppConfig（环境变量覆盖 + 默认 provider 模板补齐），
//     不构造 NovelAgentApp。
//   - initialize()/openProject()/createProject()/tryAutoStart() 触发
//     rebuildApp()：销毁旧 NovelAgentApp、用现有构造函数整体重建。
//   - agentReady 为 false 时所有 Agent 相关操作被拦截。
//
// 线程模型（串行模式）：
//   - sendMessage() 在独立工作线程中调用 Agent::process()，避免阻塞 UI
//   - StreamCallbacks 在 HTTP 线程触发，经 QMetaObject::invokeMethod
//     (Qt::QueuedConnection) 转发到 QML 主线程
//   - busy_ 原子标志保证同一时刻只有一个请求在执行（串行）
//   - worker_ 不再 detach；重建/析构前 joinWorker()（仅在 !busy_ 时调用，
//     此时 process() 已返回，join 只等待线程收尾，不会长阻塞）

#include "config/AppConfig.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>
#include <thread>

struct Project;
class NovelAgentApp;

namespace qtui {

class QmlBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool agentReady READ agentReady NOTIFY agentReadyChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString modelName READ modelName NOTIFY modelChanged)
    Q_PROPERTY(QString providerName READ providerName NOTIFY modelChanged)
    Q_PROPERTY(int totalTokens READ totalTokens NOTIFY usageChanged)
    Q_PROPERTY(int contextPercent READ contextPercent NOTIFY usageChanged)

public:
    explicit QmlBridge(QObject* parent = nullptr);
    ~QmlBridge() override;

    // ── 属性读取 ──
    bool agentReady() const { return app_ != nullptr; }
    QString projectName() const;
    QString projectPath() const;
    QString statusText() const { return status_text_; }
    bool busy() const { return busy_.load(); }
    QString modelName() const;
    QString providerName() const;
    int totalTokens() const;
    int contextPercent() const;

    // ── 会话交互（既有）──
    Q_INVOKABLE void sendMessage(const QString& text);
    Q_INVOKABLE void cancelRequest();
    Q_INVOKABLE void newSession();
    Q_INVOKABLE void refreshProject();
    Q_INVOKABLE QVariantList chapterList() const;
    Q_INVOKABLE QString loadChapter(const QString& chapterId);

    // ── 启动 / 初始化（Task 3/4 实现）──
    Q_INVOKABLE bool tryAutoStart();
    Q_INVOKABLE bool initialize(const QString& providerName);

    // ── Provider 配置（Task 3 实现）──
    Q_INVOKABLE QStringList listProviders() const;
    Q_INVOKABLE QVariantMap providerInfo(const QString& name) const;
    Q_INVOKABLE bool saveProvider(const QString& name, const QVariantMap& values);
    Q_INVOKABLE QString defaultProvider() const;
    Q_INVOKABLE bool hasUsableApiKey(const QString& name) const;

    // ── 项目操作（Task 4 实现）──
    Q_INVOKABLE QString validateProjectDir(const QString& path) const;
    Q_INVOKABLE bool openProject(const QString& path);
    Q_INVOKABLE bool createProject(const QString& dirPath, const QString& title);
    Q_INVOKABLE QString lastProjectPath() const;

    // ── 调试 ──
    Q_INVOKABLE void setVerbose(bool enabled);
    Q_INVOKABLE bool verboseEnabled() const { return config_.verbose; }

signals:
    // 流式输出（逐 token 推送到 QML）
    void tokenReceived(const QString& delta);
    void reasoningReceived(const QString& delta);
    void toolCallStarted(const QString& toolName);
    void toolCallFinished(const QString& toolName, bool ok);
    void responseComplete(const QString& fullText);
    void errorOccurred(const QString& message);

    // 状态变化
    void agentReadyChanged();
    void providersChanged();
    void projectChanged();
    void chaptersChanged();
    void statusChanged(const QString& text);
    void busyChanged();
    void modelChanged();
    void usageChanged();

private:
    void setStatus(const QString& text);
    void runAgent(std::string input);
    void joinWorker();
    // 整体重建 NovelAgentApp。providerName 必须存在于 config_ 且有 API Key；
    // project 可为 nullptr（无项目状态）。失败时保持 app_ == nullptr 并写 error。
    bool rebuildApp(const std::string& providerName,
                    std::shared_ptr<Project> project, QString* error);
    // 当前生效的 provider 名：已初始化取运行中配置，否则取 config_ 默认值。
    std::string activeProviderName() const;

    AppConfig config_;
    std::unique_ptr<NovelAgentApp> app_;
    std::shared_ptr<Project> project_;   // 与 app_->project() 保持同步

    QString status_text_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancel_requested_{false};
    std::thread worker_;
};

} // namespace qtui
```

- [ ] **Step 2.2: 改写 QmlBridge.cpp 的生命周期部分**

对 `src/novelagent_qt/QmlBridge.cpp` 做如下修改（本步只列生命周期相关，Task 3/4 的方法在各自任务给出完整代码；为使本步可编译，先为 Task 3/4 声明的 12 个 Q_INVOKABLE 提供最小占位实现——直接 `return {};` / `return false;`，Task 3/4 会替换它们）：

includes 调整：

```cpp
#include "novelagent_qt/QmlBridge.h"

#include "Bootstrap.h"
#include "NovelAgentApp.h"
#include "project/Models/Project.h"
#include "project/ProjectIO.h"
#include "project/ProjectManager.h"

#include <QMetaObject>
#include <QUrl>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
```

构造/析构与工具函数：

```cpp
namespace {

// QML FolderDialog / FileDialog 返回 file:///D:/... 形式 URL，统一转本地路径。
std::string toLocalPath(const QString& pathOrUrl) {
    if (pathOrUrl.startsWith(QStringLiteral("file:")))
        return QUrl(pathOrUrl).toLocalFile().toStdString();
    return pathOrUrl.toStdString();
}

// 占位 API Key 检测（与原 Bootstrap 校验 3 一致）。
bool isPlaceholderKey(const std::string& key) {
    return key.find("请替换") != std::string::npos ||
           key.find("your-") != std::string::npos ||
           key.find("placeholder") != std::string::npos;
}

} // namespace

QmlBridge::QmlBridge(QObject* parent)
    : QObject(parent)
{
    config_ = AppConfig::load();

    // 环境变量优先：DEEPSEEK_API_KEY / KIMI_API_KEY / CLAUDE_API_KEY
    // 覆盖配置文件中的值（仅运行时生效；用户在设置里保存才会落盘）
    for (const auto& name : {"DEEPSEEK", "KIMI", "CLAUDE"}) {
        if (const char* key = std::getenv((std::string(name) + "_API_KEY").c_str())) {
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            config_.setApiKey(lower, key);
        }
    }

    // 首次启动时补齐三家 provider 模板，供设置界面/向导下拉展示
    config_.ensureDefaultProviders();

    if (config_.verbose) spdlog::set_level(spdlog::level::debug);

    status_text_ = QStringLiteral("等待初始化");
}

QmlBridge::~QmlBridge() {
    cancel_requested_.store(true);
    if (app_) {
        // 若仍在生成，请求取消后等待工作线程退出，避免回调竞态
        // （app_ 成员销毁晚于本函数体，worker 引用的 Agent 仍有效）
        bootstrap::g_cancel_flag.store(nullptr);
    }
    joinWorker();
}

void QmlBridge::joinWorker() {
    if (worker_.joinable())
        worker_.join();
}

bool QmlBridge::rebuildApp(const std::string& providerName,
                           std::shared_ptr<Project> project, QString* error) {
    if (busy_.load()) {
        if (error) *error = QStringLiteral("Agent 正在生成中，请稍后再切换配置");
        return false;
    }

    const ProviderConfig* prov = config_.getProvider(providerName);
    if (!prov) {
        if (error) *error = QStringLiteral("未找到 provider: ")
                            + QString::fromStdString(providerName);
        return false;
    }
    if (prov->api_key.empty() || isPlaceholderKey(prov->api_key)) {
        if (error) *error = QStringLiteral("provider '%1' 尚未配置有效 API Key")
                            .arg(QString::fromStdString(providerName));
        return false;
    }

    joinWorker();

    // 先解除 SIGINT 对旧 Agent 取消标志的引用，再销毁旧实例
    bootstrap::g_cancel_flag.store(nullptr);
    app_.reset();

    try {
        app_ = std::make_unique<NovelAgentApp>(*prov, std::move(project));
    } catch (const std::exception& e) {
        if (error) *error = QStringLiteral("初始化失败: ") + QString::fromUtf8(e.what());
        emit agentReadyChanged();
        emit projectChanged();
        setStatus(QStringLiteral("初始化失败"));
        return false;
    }

    project_ = app_->project();
    bootstrap::g_cancel_flag.store(app_->agent().cancelFlag());

    emit agentReadyChanged();
    emit projectChanged();
    emit chaptersChanged();
    emit modelChanged();
    emit usageChanged();
    setStatus(QStringLiteral("就绪"));
    return true;
}

std::string QmlBridge::activeProviderName() const {
    if (app_) return app_->agent().client().config().name;
    return config_.default_provider;
}
```

属性读取改为空安全：

```cpp
QString QmlBridge::projectName() const {
    if (project_ && !project_->title.empty())
        return QString::fromStdString(project_->title);
    return QStringLiteral("未打开项目");
}

QString QmlBridge::projectPath() const {
    if (project_ && !project_->path.empty())
        return QString::fromStdString(project_->path);
    return {};
}

QString QmlBridge::modelName() const {
    if (app_) return QString::fromStdString(app_->agent().client().config().model);
    if (const auto* p = config_.getDefaultProvider())
        return QString::fromStdString(p->model);
    return {};
}

QString QmlBridge::providerName() const {
    return QString::fromStdString(activeProviderName());
}
```

既有槽函数加防护（修改点）：

```cpp
void QmlBridge::sendMessage(const QString& text) {
    if (text.trimmed().isEmpty()) return;
    if (!app_) {
        emit errorOccurred(QStringLiteral("尚未完成初始化，请先在设置中配置模型"));
        return;
    }
    if (busy_.load()) {
        spdlog::warn("[QmlBridge] 忽略重复请求（Agent 正忙）");
        return;
    }
    runAgent(text.toStdString());
}

void QmlBridge::cancelRequest() {
    if (app_ && busy_.load()) {
        app_->agent().requestCancel();
        setStatus(QStringLiteral("正在取消..."));
    }
}

void QmlBridge::newSession() {
    if (!app_ || busy_.load()) return;
    app_->agent().resetSession();
    emit usageChanged();
    setStatus(QStringLiteral("新会话已创建"));
}
```

`chapterList()` / `loadChapter()`：函数体不变，仅开头的 `if (!project_)` 防护已天然成立（project_ 初始为 nullptr）。

`runAgent()` 修改三处：
1. 函数开头加 `joinWorker();`（回收上一次已结束的线程）；
2. 所有 `agent_.` 调用改为 `app_->agent().`（`resetCancel`、`process`）；
3. 删掉末尾的 `worker_.detach();`（保持 joinable，由 joinWorker 统一回收）。

Task 3/4 方法的临时占位（本步末尾追加，Task 3/4 替换）：

```cpp
// ── 以下方法在 Task 3 / Task 4 中实现 ──
bool QmlBridge::tryAutoStart() { return false; }
bool QmlBridge::initialize(const QString&) { return false; }
QStringList QmlBridge::listProviders() const { return {}; }
QVariantMap QmlBridge::providerInfo(const QString&) const { return {}; }
bool QmlBridge::saveProvider(const QString&, const QVariantMap&) { return false; }
QString QmlBridge::defaultProvider() const { return {}; }
bool QmlBridge::hasUsableApiKey(const QString&) const { return false; }
QString QmlBridge::validateProjectDir(const QString&) const { return {}; }
bool QmlBridge::openProject(const QString&) { return false; }
bool QmlBridge::createProject(const QString&, const QString&) { return false; }
QString QmlBridge::lastProjectPath() const { return {}; }
void QmlBridge::setVerbose(bool) {}
```

注意：本步引用了 `bootstrap::g_cancel_flag`（`std::atomic<std::atomic<bool>*>` 类型），该定义在 Task 5 才改。**执行顺序上先做 Task 5 的 Step 5.1（Bootstrap.h 重写）再编译本任务**，或将 Task 2 与 Task 5 一起编译验证。推荐：Step 2.2 完成后直接做 Task 5，再统一编译。

- [ ] **Step 2.3: 提交（与 Task 5 合并编译验证后）**

```bash
git add src/novelagent_qt/QmlBridge.h src/novelagent_qt/QmlBridge.cpp
git commit -m "refactor(qt): QmlBridge 持有 NovelAgentApp 并支持整体重建"
```

---

### Task 5（提前执行）: Bootstrap 瘦身 + 入口改造 + 移除 CLI11

> 注：编号保持与文件结构表一致，但**实际执行顺序为 Task 1 → 2 → 5 → 3 → 4 → 6 → 7 → 8 → 9**，因为 Task 2 依赖新 Bootstrap.h 的符号。

**Files:**
- Rewrite: `src/Bootstrap.h`
- Modify: `src/main_gui.cpp`
- Modify: `src/novelagent_qt/QmlApp.h`
- Modify: `src/novelagent_qt/QmlApp.cpp`
- Modify: `CMakeLists.txt`（COMMON_LIBS）
- Modify: `cmake/FetchDependencies.cmake`

- [ ] **Step 5.1: 重写 Bootstrap.h**

替换 `src/Bootstrap.h` 全文为：

```cpp
#pragma once

// ============================================================================
// Bootstrap — 瘦身后只保留 SIGINT（Ctrl+C）优雅退出支持。
//
// 配置加载、Provider 校验、项目打开/创建、NovelAgentApp 构造
// 已全部迁移到 qtui::QmlBridge（GUI 内完成，见 QmlBridge::rebuildApp）。
// CLI 参数解析随 CLI11 依赖一并移除。
// ============================================================================

#include <atomic>
#include <csignal>

namespace bootstrap {

// 指向当前 Agent 的取消标志。QmlBridge 每次重建 NovelAgentApp 后更新；
// 销毁旧实例前先置空，保证信号处理函数不会访问悬垂指针。
inline std::atomic<std::atomic<bool>*> g_cancel_flag{nullptr};

// SIGINT 处理：只做原子读写，通知 Agent 主循环自行清理退出。
extern "C" inline void sigint_handler(int) {
    if (auto* flag = g_cancel_flag.load()) flag->store(true);
}

inline void installSigint() {
    signal(SIGINT, sigint_handler);
}

} // namespace bootstrap
```

- [ ] **Step 5.2: 改写 main_gui.cpp**

替换 `src/main_gui.cpp` 全文为：

```cpp
// novelagent_gui — QML GUI 入口。
// 依赖 Qt6 Quick / QuickControls2。
// 所有启动配置（provider / 项目 / 日志级别）由 QmlBridge 在 GUI 内完成。
#include "Bootstrap.h"
#include "novelagent_qt/QmlApp.h"

int main(int argc, char** argv) {
    bootstrap::installSigint();
    return qtui::runQmlApp(argc, argv);
}
```

- [ ] **Step 5.3: 改写 QmlApp**

`src/novelagent_qt/QmlApp.h` 替换全文：

```cpp
#pragma once

// QmlApp — QML 应用入口。
//
// 创建 QGuiApplication + QQmlApplicationEngine，注册 QmlBridge 到 QML context，
// 加载 MainWindow.qml。由 main_gui.cpp 调用。
// QmlBridge 自行加载 AppConfig 并按需构造 NovelAgentApp（延迟到用户配置完成）。

namespace qtui {

// 启动 QML GUI 应用（阻塞直到窗口关闭），返回进程退出码。
int runQmlApp(int argc, char** argv);

} // namespace qtui
```

`src/novelagent_qt/QmlApp.cpp`：签名改为 `int runQmlApp(int argc, char** argv)`，删除 `#include "NovelAgentApp.h"`，桥接对象构造改为：

```cpp
    QmlBridge bridge;
```

其余（engine、context property、load url、objectCreated 兜底）不变。

- [ ] **Step 5.4: 移除 CLI11**

1. `CMakeLists.txt` 第 60 行 `COMMON_LIBS` 中删除 `CLI11::CLI11`；
2. `cmake/FetchDependencies.cmake` 删除第 21-32 行的 `# --- CLI11 ---` 整块；
3. 确认无残留引用：`rg "CLI11|CLI/CLI.hpp" --glob "!build/**" --glob "!_ref/**"` 应无 src/cmake 命中。

- [ ] **Step 5.5: 编译验证（Task 2 + Task 5 联合）**

Run: `cmake --build build --target novelagent_gui`
Expected: 编译链接成功。启动 `build/novelagent_gui.exe`：窗口打开、状态栏显示"等待初始化"、发送消息弹错误提示（占位实现阶段属预期）。

- [ ] **Step 5.6: 提交**

```bash
git add src/Bootstrap.h src/main_gui.cpp src/novelagent_qt/QmlApp.h src/novelagent_qt/QmlApp.cpp CMakeLists.txt cmake/FetchDependencies.cmake
git commit -m "refactor(bootstrap): Bootstrap 瘦身为 SIGINT 骨架，移除 CLI11 依赖"
```

---

### Task 3: QmlBridge Provider 配置方法

**Files:**
- Modify: `src/novelagent_qt/QmlBridge.cpp`（替换 Task 2 的占位实现）

- [ ] **Step 3.1: 实现 Provider 相关方法**

用以下实现替换对应占位：

```cpp
QStringList QmlBridge::listProviders() const {
    QStringList list;
    for (const auto& [name, cfg] : config_.providers)
        list << QString::fromStdString(name);
    return list;
}

QVariantMap QmlBridge::providerInfo(const QString& name) const {
    QVariantMap m;
    const ProviderConfig* p = config_.getProvider(name.toStdString());
    if (!p) return m;
    m.insert(QStringLiteral("name"), QString::fromStdString(p->name));
    m.insert(QStringLiteral("api_key"), QString::fromStdString(p->api_key));
    m.insert(QStringLiteral("base_url"), QString::fromStdString(p->base_url));
    m.insert(QStringLiteral("model"), QString::fromStdString(p->model));
    m.insert(QStringLiteral("temperature"), p->temperature);
    m.insert(QStringLiteral("max_tokens"), p->max_tokens);
    m.insert(QStringLiteral("enable_thinking"), p->enable_thinking);
    m.insert(QStringLiteral("hasKey"),
             !p->api_key.empty() && !isPlaceholderKey(p->api_key));
    m.insert(QStringLiteral("isDefault"), config_.default_provider == p->name);
    return m;
}

bool QmlBridge::saveProvider(const QString& name, const QVariantMap& v) {
    const std::string key = name.toStdString();
    if (key.empty()) return false;
    ProviderConfig& p = config_.providers[key];
    p.name = key;
    if (v.contains(QStringLiteral("api_key")))
        p.api_key = v[QStringLiteral("api_key")].toString().toStdString();
    if (v.contains(QStringLiteral("base_url")))
        p.base_url = v[QStringLiteral("base_url")].toString().toStdString();
    if (v.contains(QStringLiteral("model")))
        p.model = v[QStringLiteral("model")].toString().toStdString();
    if (v.contains(QStringLiteral("temperature")))
        p.temperature = v[QStringLiteral("temperature")].toDouble();
    if (v.contains(QStringLiteral("max_tokens")))
        p.max_tokens = v[QStringLiteral("max_tokens")].toInt();
    if (v.contains(QStringLiteral("enable_thinking")))
        p.enable_thinking = v[QStringLiteral("enable_thinking")].toBool();
    config_.save();
    emit providersChanged();
    return true;
}

QString QmlBridge::defaultProvider() const {
    return QString::fromStdString(config_.default_provider);
}

bool QmlBridge::hasUsableApiKey(const QString& name) const {
    const ProviderConfig* p = config_.getProvider(name.toStdString());
    return p && !p->api_key.empty() && !isPlaceholderKey(p->api_key);
}

bool QmlBridge::initialize(const QString& providerName) {
    const std::string name = providerName.toStdString();
    QString err;
    // 沿用当前项目（可为 nullptr，即"无项目"状态）
    if (!rebuildApp(name, project_, &err)) {
        emit errorOccurred(err);
        return false;
    }
    config_.default_provider = name;
    config_.save();
    return true;
}

void QmlBridge::setVerbose(bool enabled) {
    config_.verbose = enabled;
    spdlog::set_level(enabled ? spdlog::level::debug : spdlog::level::info);
    config_.save();
}
```

- [ ] **Step 3.2: 编译验证**

Run: `cmake --build build --target novelagent_gui`
Expected: 编译成功

- [ ] **Step 3.3: 提交**

```bash
git add src/novelagent_qt/QmlBridge.cpp
git commit -m "feat(qt): QmlBridge 新增 Provider 配置读写与 initialize"
```

---

### Task 4: QmlBridge 项目操作 + 自动启动

**Files:**
- Modify: `src/novelagent_qt/QmlBridge.cpp`（替换剩余占位实现）

- [ ] **Step 4.1: 实现项目方法与 tryAutoStart**

```cpp
QString QmlBridge::validateProjectDir(const QString& path) const {
    const std::string dir = toLocalPath(path);
    if (dir.empty()) return QStringLiteral("invalid");
    ProjectManager pm;
    if (pm.isValid(dir)) return QStringLiteral("valid");      // 已是小说项目，可打开
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return QStringLiteral("new");   // 不存在，可创建
    if (fs::is_directory(dir, ec) && fs::is_empty(dir, ec))
        return QStringLiteral("new");                          // 空目录，可创建
    return QStringLiteral("occupied");                         // 非空且非项目
}

bool QmlBridge::openProject(const QString& path) {
    const std::string dir = toLocalPath(path);
    ProjectManager pm;
    Project p = pm.open(dir);
    if (p.title.empty()) {
        emit errorOccurred(QStringLiteral("无法打开项目（目录中缺少有效的 novel.json）: ")
                           + QString::fromStdString(dir));
        return false;
    }
    QString err;
    if (!rebuildApp(activeProviderName(),
                    std::make_shared<Project>(std::move(p)), &err)) {
        emit errorOccurred(err);
        return false;
    }
    config_.last_project_path = dir;
    config_.save();
    return true;
}

bool QmlBridge::createProject(const QString& dirPath, const QString& title) {
    const std::string dir = toLocalPath(dirPath);
    const std::string name = title.trimmed().toStdString();
    if (dir.empty() || name.empty()) {
        emit errorOccurred(QStringLiteral("项目路径和名称不能为空"));
        return false;
    }
    ProjectManager pm;
    Project p = pm.create(dir, name);
    if (p.title.empty()) {
        emit errorOccurred(QStringLiteral("创建项目失败: ") + QString::fromStdString(dir));
        return false;
    }
    QString err;
    if (!rebuildApp(activeProviderName(),
                    std::make_shared<Project>(std::move(p)), &err)) {
        emit errorOccurred(err);
        return false;
    }
    config_.last_project_path = dir;
    config_.save();
    return true;
}

QString QmlBridge::lastProjectPath() const {
    return QString::fromStdString(config_.last_project_path);
}

bool QmlBridge::tryAutoStart() {
    const ProviderConfig* prov = config_.getDefaultProvider();
    // 缺默认 provider 或无有效 key → 交给 QML 打开首启向导
    if (!prov || prov->api_key.empty() || isPlaceholderKey(prov->api_key))
        return false;

    // 上次项目仍有效则自动恢复；无效则以"无项目"状态启动
    std::shared_ptr<Project> project;
    if (!config_.last_project_path.empty()) {
        ProjectManager pm;
        if (pm.isValid(config_.last_project_path)) {
            Project p = pm.open(config_.last_project_path);
            if (!p.title.empty())
                project = std::make_shared<Project>(std::move(p));
        }
    }

    QString err;
    if (!rebuildApp(config_.default_provider, std::move(project), &err)) {
        spdlog::warn("[QmlBridge] 自动启动失败: {}", err.toStdString());
        return false;
    }
    return true;
}
```

文件顶部补 `#include <filesystem>`。

- [ ] **Step 4.2: 编译验证**

Run: `cmake --build build --target novelagent_gui`
Expected: 编译成功。手动验证：工作区已有 `config.json`（含有效 key），在 QML 调试控制台或临时按钮调用 `bridge.tryAutoStart()` 应返回 true 且状态栏变"就绪"（正式入口在 Task 8 接线）。

- [ ] **Step 4.3: 提交**

```bash
git add src/novelagent_qt/QmlBridge.cpp
git commit -m "feat(qt): QmlBridge 新增项目打开/创建与 tryAutoStart 自动恢复"
```

---

### Task 6: SettingsDialog.qml（设置对话框）

**Files:**
- Create: `src/novelagent_qt/qml/SettingsDialog.qml`

- [ ] **Step 6.1: 新建 SettingsDialog.qml**

```qml
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
```

- [ ] **Step 6.2: 提交**

```bash
git add src/novelagent_qt/qml/SettingsDialog.qml
git commit -m "feat(qml): 新增 SettingsDialog 设置对话框"
```

---

### Task 7: WelcomeWizard.qml（首次启动向导）

**Files:**
- Create: `src/novelagent_qt/qml/WelcomeWizard.qml`

- [ ] **Step 7.1: 新建 WelcomeWizard.qml**

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// WelcomeWizard — 首次启动向导：欢迎 → 配置模型 → 选择项目。
// tryAutoStart() 失败（无有效默认 Provider）时由 MainWindow 打开。
Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 520
    height: 480
    modal: true
    closePolicy: Popup.NoAutoClose
    padding: Theme.gapLg

    background: Rectangle {
        color: Theme.bgElevated
        radius: Theme.radiusMd
        border.width: 1
        border.color: Theme.divider
    }

    onOpened: {
        pages.currentIndex = 0
        providerCombo.model = bridge.listProviders()
        var idx = providerCombo.model.indexOf(bridge.defaultProvider())
        providerCombo.currentIndex = idx >= 0 ? idx : 0
        loadProviderFields()
    }

    function loadProviderFields() {
        var info = bridge.providerInfo(providerCombo.currentText)
        keyField.text = info.hasKey ? info.api_key : ""
        modelField.text = info.model || ""
    }

    // 保存 provider 并初始化 Agent；成功返回 true
    function applyProvider() {
        bridge.saveProvider(providerCombo.currentText, {
            "api_key": keyField.text.trim(),
            "model": modelField.text.trim()
        })
        return bridge.initialize(providerCombo.currentText)
    }

    StackLayout {
        id: pages
        anchors.fill: parent

        // ── 第 0 页：欢迎 ──
        ColumnLayout {
            spacing: Theme.gapMd
            Item { Layout.fillHeight: true }
            Label {
                text: "墨染"
                font.family: Theme.fontDisplay
                font.pixelSize: 42
                font.weight: Font.Bold
                color: Theme.textPrimary
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: "AI 小说创作助手\n首次使用需要完成两步配置：模型与项目"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeUi
                color: Theme.textSecondary
                horizontalAlignment: Text.AlignHCenter
                Layout.alignment: Qt.AlignHCenter
            }
            Item { Layout.fillHeight: true }
            Button {
                text: "开始配置"
                highlighted: true
                Layout.alignment: Qt.AlignHCenter
                onClicked: pages.currentIndex = 1
            }
        }

        // ── 第 1 页：模型配置 ──
        ColumnLayout {
            spacing: Theme.gapMd
            Label {
                text: "第 1 步 / 共 2 步：配置模型"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textFaint
            }
            Label {
                text: "选择 Provider"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textSecondary
            }
            ComboBox {
                id: providerCombo
                Layout.fillWidth: true
                onActivated: root.loadProviderFields()
            }
            Label {
                text: "API Key"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textSecondary
            }
            TextField {
                id: keyField
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: "sk-..."
            }
            Label {
                text: "模型名"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textSecondary
            }
            TextField { id: modelField; Layout.fillWidth: true }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { text: "上一步"; onClicked: pages.currentIndex = 0 }
                Button {
                    text: "下一步"
                    highlighted: true
                    enabled: keyField.text.trim().length > 0
                    onClicked: {
                        if (root.applyProvider())
                            pages.currentIndex = 2
                        // 失败时 bridge 会 emit errorOccurred，由聊天面板错误提示展示
                    }
                }
            }
        }

        // ── 第 2 页：项目 ──
        ColumnLayout {
            spacing: Theme.gapMd
            Label {
                text: "第 2 步 / 共 2 步：选择项目"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textFaint
            }
            Button {
                text: "打开已有项目..."
                Layout.fillWidth: true
                onClicked: wizardOpenDlg.open()
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
            Label {
                text: "或新建项目"
                font.family: Theme.fontUi
                font.pixelSize: Theme.sizeCaption
                color: Theme.textSecondary
            }
            TextField {
                id: wizardTitleField
                Layout.fillWidth: true
                placeholderText: "小说名称"
            }
            RowLayout {
                TextField {
                    id: wizardDirField
                    Layout.fillWidth: true
                    readOnly: true
                    placeholderText: "项目目录"
                }
                Button { text: "浏览..."; onClicked: wizardNewDlg.open() }
            }
            Button {
                text: "创建并进入"
                highlighted: true
                Layout.fillWidth: true
                enabled: wizardTitleField.text.trim().length > 0 && wizardDirField.text.length > 0
                onClicked: {
                    if (bridge.createProject(wizardDirField.text, wizardTitleField.text))
                        root.close()
                }
            }
            Item { Layout.fillHeight: true }
            Button {
                text: "暂时跳过（稍后可在设置中打开项目）"
                flat: true
                Layout.alignment: Qt.AlignHCenter
                onClicked: root.close()   // Agent 已在第 1 步初始化，无项目状态可用
            }
        }
    }

    FolderDialog {
        id: wizardOpenDlg
        title: "选择小说项目目录"
        onAccepted: {
            if (bridge.openProject(selectedFolder.toString()))
                root.close()
        }
    }

    FolderDialog {
        id: wizardNewDlg
        title: "选择新项目目录"
        onAccepted: {
            var status = bridge.validateProjectDir(selectedFolder.toString())
            if (status !== "occupied")
                wizardDirField.text = selectedFolder.toString()
        }
    }
}
```

- [ ] **Step 7.2: 提交**

```bash
git add src/novelagent_qt/qml/WelcomeWizard.qml
git commit -m "feat(qml): 新增 WelcomeWizard 首次启动向导"
```

---

### Task 8: 主界面装配（MainWindow / Sidebar / StatusBar / AgentPanel / CMake）

**Files:**
- Modify: `CMakeLists.txt`（NOVELAGENT_QML_FILES 列表）
- Modify: `src/novelagent_qt/qml/MainWindow.qml`
- Modify: `src/novelagent_qt/qml/SidebarPanel.qml`
- Modify: `src/novelagent_qt/qml/StatusBar.qml`
- Modify: `src/novelagent_qt/qml/AgentPanel.qml`

- [ ] **Step 8.1: CMake 注册新 QML 文件**

`CMakeLists.txt` 的 `NOVELAGENT_QML_FILES` 列表中、`Theme.qml` 之前追加两行：

```cmake
src/novelagent_qt/qml/SettingsDialog.qml
src/novelagent_qt/qml/WelcomeWizard.qml
```

- [ ] **Step 8.2: SidebarPanel 激活设置入口**

`src/novelagent_qt/qml/SidebarPanel.qml`：
1. 根 `Rectangle { id: root` 声明下加一行信号：

```qml
    signal settingsRequested()
```

2. 齿轮的 `ToolTip.text: "设置功能开发中"` 改为 `ToolTip.text: "设置"`；
3. `MouseArea { id: settingsMa ... }` 内追加：

```qml
                    onClicked: root.settingsRequested()
```

- [ ] **Step 8.3: StatusBar Provider 区域可点击**

`src/novelagent_qt/qml/StatusBar.qml`：
1. 根 `Rectangle { id: root` 声明下加：

```qml
    signal settingsRequested()
```

2. 将末尾的 "Provider · 模型" Label（第 81-86 行）替换为：

```qml
        // Provider · 模型（点击打开模型设置）
        Label {
            text: (bridge.providerName === "" ? "未配置" : bridge.providerName)
                  + " · " + (bridge.modelName === "" ? "—" : bridge.modelName)
            font.family: Theme.fontUi
            font.pixelSize: Theme.sizeCaption
            color: providerMa.containsMouse ? Theme.textPrimary : Theme.textFaint

            MouseArea {
                id: providerMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.settingsRequested()
            }
        }
```

- [ ] **Step 8.4: AgentPanel 未初始化时禁用发送**

`src/novelagent_qt/qml/AgentPanel.qml`：
1. `sendBtn` 的 enabled（约 285 行）改为：

```qml
                    enabled: bridge.agentReady && (bridge.busy || inputField.text.trim().length > 0)
```

2. `inputField`（TextArea，约 217 行）的占位文案改为条件表达式（若原先无 placeholderText 则新增）：

```qml
                placeholderText: bridge.agentReady ? "输入创作指令..." : "请先完成模型配置（左下角设置）"
```

- [ ] **Step 8.5: MainWindow 装配**

`src/novelagent_qt/qml/MainWindow.qml`：
1. `SidebarPanel { ... }` 增加信号处理：

```qml
            SidebarPanel {
                SplitView.preferredWidth: 240
                SplitView.minimumWidth: 200
                SplitView.maximumWidth: 320
                onSettingsRequested: settingsDialog.openAt(0)
            }
```

2. `footer: StatusBar {}` 改为：

```qml
    footer: StatusBar {
        onSettingsRequested: settingsDialog.openAt(0)
    }
```

3. 在 `Shortcut` 声明之前追加：

```qml
    SettingsDialog { id: settingsDialog }
    WelcomeWizard { id: welcomeWizard }

    // 启动策略：有默认 Provider + 有效 Key → 自动初始化（并恢复上次项目）；
    // 否则打开首启向导。
    Component.onCompleted: {
        if (!bridge.tryAutoStart())
            welcomeWizard.open()
    }
```

- [ ] **Step 8.6: 编译 + 冒烟验证**

Run: `cmake --build build --target novelagent_gui`
Expected: 编译成功；启动 `build/novelagent_gui.exe`：
- 工作区 `config.json` 有有效 key → 直接进入"就绪"（首次运行 last_project_path 为空 → 无项目状态）
- 把 config.json 临时改名后再启动 → 弹出欢迎向导

- [ ] **Step 8.7: 提交**

```bash
git add CMakeLists.txt src/novelagent_qt/qml
git commit -m "feat(qml): 主界面接入设置对话框与首启向导，实现 GUI 内配置闭环"
```

---

### Task 9: 窗口状态持久化（P2，可选）

**Files:**
- Modify: `src/novelagent_qt/qml/MainWindow.qml`

- [ ] **Step 9.1: 用 QtCore Settings 持久化窗口几何**

`MainWindow.qml` 头部加 `import QtCore`，在 `ApplicationWindow` 内追加：

```qml
    Settings {
        category: "MainWindow"
        property alias windowX: window.x
        property alias windowY: window.y
        property alias windowWidth: window.width
        property alias windowHeight: window.height
    }
```

（QSettings 默认写入注册表 `HKCU\Software\NovelAgent`，无需额外配置。）

- [ ] **Step 9.2: 验证 + 提交**

Run: 启动 → 调整窗口大小/位置 → 关闭 → 重启，几何应恢复。

```bash
git add src/novelagent_qt/qml/MainWindow.qml
git commit -m "feat(qml): 窗口位置与大小持久化"
```

---

## 手动端到端验证清单（全部任务完成后）

| # | 场景 | 步骤 | 预期 |
|---|------|------|------|
| 1 | 首次启动 | 临时移走 `./config.json` 与 `~/.novelagent/config.json` 后启动 | 弹出欢迎向导；完成两步后进入"就绪"；`~/.novelagent/config.json` 生成且含 key / last_project_path |
| 2 | 二次启动自动恢复 | 直接重启 | 不弹向导，自动加载上次项目，状态栏"就绪" |
| 3 | 切换 Provider | 设置 → 模型 → 选 kimi → 填 key → "保存并启用" | 状态栏 Provider·模型即时更新；会话重置；config.json 的 default_provider 变为 kimi |
| 4 | 新建项目 | 设置 → 项目 → 新建（选空目录 + 输入名称） | 侧栏项目名更新；目录内生成 novel.json 等；last_project_path 更新 |
| 5 | 打开无效目录 | 设置 → 项目 → 打开一个非项目目录 | 弹错误提示"缺少有效的 novel.json"，当前状态不变 |
| 6 | 忙时切换拦截 | 发送长生成请求，期间尝试"保存并启用" | 报"Agent 正在生成中"，不崩溃 |
| 7 | 未初始化拦截 | 向导第 2 页点"暂时跳过"之前观察输入框 | 初始化前发送按钮禁用、占位文案提示先配置 |
| 8 | 环境变量覆盖 | `$env:DEEPSEEK_API_KEY="sk-test"` 后启动 | 运行时生效；不点保存不写入 config.json |
| 9 | Ctrl+C（控制台启动） | 从 pwsh 启动 exe，生成中按 Ctrl+C | 当前生成被取消，进程不崩溃 |
| 10 | 单测回归 | `ctest --test-dir build --output-on-failure` | 全部通过 |

## 风险与已知取舍

1. **切换 Provider/项目会清空当前会话记忆** — 整体重建的固有语义，与"新建会话"一致，"保存并启用"即隐含开新会话。
2. **`saveProvider` 会把环境变量注入的 key 落盘**（若用户在设置里点保存）— 本地单用户应用，可接受。
3. **join 时机**：`rebuildApp` / `runAgent` 只在 `busy_ == false` 时 join，此时 `Agent::process()` 已返回，join 仅等待线程收尾（微秒级），不阻塞 UI。
4. **repo 根目录 `config.json` 优先级**：`AppConfig::load()` 先读 CWD。开发时从工作区启动会读写根目录 config.json（`source_path` 回写保证不会错写到全局路径）；发布环境 CWD 无 config.json，自然落到 `~/.novelagent/`。
5. 完成后应把 `docs/review/BOOTSTRAP_SLIM_PLAN_2026-07-27.md` 的状态行更新为"✅ 已执行（方案 C：整体重建，见本计划）"。

## 执行顺序总览

```
Task 1 (AppConfig) ──► Task 2 (QmlBridge 生命周期) ──► Task 5 (Bootstrap/入口/CLI11)
                                                            │  ← 联合编译点
                       Task 3 (Provider 方法) ──► Task 4 (项目方法)
                                                            │
      Task 6 (SettingsDialog) ──► Task 7 (WelcomeWizard) ──► Task 8 (装配) ──► Task 9 (P2)
```
