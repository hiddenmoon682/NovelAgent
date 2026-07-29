// QmlBridge 实现 — 拥有 NovelAgentApp 生命周期 + 串行模式线程安全桥接。

#include "novelagent_qt/QmlBridge.h"

#include "Bootstrap.h"
#include "NovelAgentApp.h"
#include "agent/index/IIndexService.h"
#include "agent/session/SessionPersistence.h"
#include "project/Models/Project.h"
#include "project/ProjectIO.h"
#include "project/ProjectManager.h"

#include <QMetaObject>
#include <QUrl>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace qtui {

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

// ── 生命周期 ──

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
    emit skillsChanged();
    setStatus(QStringLiteral("就绪"));
    return true;
}

std::string QmlBridge::activeProviderName() const {
    if (app_) return app_->agent().client().config().name;
    return config_.default_provider;
}

// ── 属性读取 ──

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

int QmlBridge::totalTokens() const {
    return app_ ? app_->agent().contextUsage().total_tokens : 0;
}

int QmlBridge::contextPercent() const {
    return app_ ? app_->agent().contextUsage().percent : 0;
}

// ── QML 槽 ──

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
    app_->agent().resetSession();  // 旧会话保留在列表中，新建空会话并切换
    emit sessionReset();
    emit sessionsChanged();
    emit usageChanged();
    setStatus(QStringLiteral("新会话已创建"));
}

QVariantList QmlBridge::sessionList() const {
    QVariantList list;
    if (!app_) return list;
    auto* persistence = app_->agent().persistence();
    if (!persistence) return list;  // 未打开项目时无持久化，无会话列表

    // 持久层异常（磁盘 IO/重建失败）不得穿透 Q_INVOKABLE 进 QML 引擎
    try {
        const std::string active = persistence->activeSessionId();
        for (const auto& s : persistence->listSessions()) {
            QVariantMap m;
            m.insert(QStringLiteral("id"), QString::fromStdString(s.id));
            m.insert(QStringLiteral("title"), s.title.empty()
                         ? QStringLiteral("新会话")
                         : QString::fromStdString(s.title));
            m.insert(QStringLiteral("active"), s.id == active);
            m.insert(QStringLiteral("updatedAt"), QString::fromStdString(s.updated_at));
            list.push_back(m);
        }
    } catch (const std::exception& e) {
        spdlog::warn("[QmlBridge] 读取会话列表失败: {}", e.what());
    }
    return list;
}

bool QmlBridge::switchSession(const QString& sessionId) {
    if (!app_ || busy_.load()) return false;
    if (!app_->agent().switchSession(sessionId.toStdString())) return false;
    emit sessionReset();
    emit sessionsChanged();
    emit usageChanged();
    setStatus(QStringLiteral("已切换会话"));
    return true;
}

bool QmlBridge::deleteSession(const QString& sessionId) {
    if (!app_ || busy_.load()) return false;
    auto* persistence = app_->agent().persistence();
    bool wasActive = false;
    try {
        wasActive =
            persistence && persistence->activeSessionId() == sessionId.toStdString();
    } catch (const std::exception& e) {
        spdlog::warn("[QmlBridge] 查询 active 会话失败: {}", e.what());
        return false;
    }
    if (!app_->agent().deleteSession(sessionId.toStdString())) return false;
    emit sessionsChanged();
    if (wasActive) {
        // active 被删后 Agent 已重载新 active 会话，聊天流需重建
        emit sessionReset();
        emit usageChanged();
    }
    setStatus(QStringLiteral("会话已删除（内容归档到 archive/）"));
    return true;
}

QVariantList QmlBridge::conversationHistory() const {
    QVariantList list;
    if (!app_) return list;

    for (const auto& msg : app_->agent().memory().messages()) {
        const bool isUser = msg.role == llm::MessageRole::User;
        const bool isAssistant = msg.role == llm::MessageRole::Assistant;
        if (!isUser && !isAssistant) continue;          // 跳过 tool 结果消息
        if (msg.content.empty()) continue;              // 跳过纯 tool_calls 占位消息
        QVariantMap m;
        m.insert(QStringLiteral("role"), isUser ? QStringLiteral("user")
                                                : QStringLiteral("assistant"));
        m.insert(QStringLiteral("content"), QString::fromStdString(msg.content));
        m.insert(QStringLiteral("reasoning"), QString::fromStdString(msg.reasoning_content));
        list.push_back(m);
    }
    return list;
}

void QmlBridge::refreshProject() {
    emit projectChanged();
    emit chaptersChanged();
}

void QmlBridge::rebuildIndex() {
    if (!app_) {
        emit errorOccurred(QStringLiteral("尚未完成初始化，无法重建索引"));
        return;
    }
    if (!project_ || project_->path.empty()) {
        emit errorOccurred(QStringLiteral("未打开项目，无法重建索引"));
        return;
    }
    if (busy_.load()) {
        spdlog::warn("[QmlBridge] 忽略重建索引请求（Agent 正忙）");
        return;
    }

    joinWorker();
    busy_.store(true);
    emit busyChanged();
    setStatus(QStringLiteral("正在重建索引..."));

    worker_ = std::thread([this]() {
        runIndexUpdate(/*force=*/true);
        QMetaObject::invokeMethod(this, [this]() {
            busy_.store(false);
            emit busyChanged();
        }, Qt::QueuedConnection);
    });
}

QVariantList QmlBridge::chapterList() const {
    QVariantList list;
    if (!project_) return list;

    std::vector<const Chapter*> sorted;
    sorted.reserve(project_->outline.chapters.size());
    for (const auto& ch : project_->outline.chapters)
        sorted.push_back(&ch);
    std::sort(sorted.begin(), sorted.end(),
              [](const Chapter* a, const Chapter* b) { return a->order < b->order; });

    for (const auto* ch : sorted) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), QString::fromStdString(ch->id));
        m.insert(QStringLiteral("title"), ch->title.empty()
                     ? QStringLiteral("第 %1 章").arg(ch->order)
                     : QString::fromStdString(ch->title));
        m.insert(QStringLiteral("order"), ch->order);
        m.insert(QStringLiteral("wordCount"), ch->word_count);
        list.push_back(m);
    }
    return list;
}

QString QmlBridge::loadChapter(const QString& chapterId) {
    if (!project_) return {};
    const std::string id = chapterId.toStdString();
    for (const auto& ch : project_->outline.chapters) {
        if (ch.id != id) continue;
        if (ch.file_path.empty()) {
            emit errorOccurred(QStringLiteral("章节尚未写入正文文件"));
            return {};
        }
        const std::string content = ProjectIO::readChapter(project_->path, ch.file_path);
        if (content.empty())
            emit errorOccurred(QStringLiteral("章节文件不存在或为空: ")
                               + QString::fromStdString(ch.file_path));
        return QString::fromStdString(content);
    }
    emit errorOccurred(QStringLiteral("未找到章节: ") + chapterId);
    return {};
}

// ── 技能管理 ──

QVariantList QmlBridge::skillList() const {
    QVariantList list;
    if (!app_) return list;

    for (const auto& s : app_->skillRegistry().listSkills()) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), QString::fromStdString(s.name));
        m.insert(QStringLiteral("description"), QString::fromStdString(s.description));
        m.insert(QStringLiteral("emoji"), QString::fromStdString(s.emoji));
        m.insert(QStringLiteral("always"), s.always);
        m.insert(QStringLiteral("enabled"), s.enabled);
        list.push_back(m);
    }
    return list;
}

bool QmlBridge::setSkillEnabled(const QString& name, bool enabled) {
    if (!app_) return false;
    if (busy_.load()) {
        emit errorOccurred(QStringLiteral("生成中无法切换技能，请稍后再试"));
        return false;
    }
    if (!app_->setSkillEnabled(name.toStdString(), enabled))
        return false;
    emit skillsChanged();
    return true;
}

// ── 内部 ──

void QmlBridge::setStatus(const QString& text) {
    status_text_ = text;
    emit statusChanged(text);
}

void QmlBridge::runAgent(std::string input) {
    joinWorker();  // 回收上一次已结束的线程

    busy_.store(true);
    cancel_requested_.store(false);
    app_->agent().resetCancel();
    emit busyChanged();
    setStatus(QStringLiteral("思考中..."));

    worker_ = std::thread([this, input = std::move(input)]() {
        llm::StreamCallbacks cb;

        cb.on_content = [this](const std::string& delta) {
            QString d = QString::fromStdString(delta);
            QMetaObject::invokeMethod(this, [this, d]() {
                emit tokenReceived(d);
            }, Qt::QueuedConnection);
        };

        cb.on_reasoning = [this](const std::string& delta) {
            QString d = QString::fromStdString(delta);
            QMetaObject::invokeMethod(this, [this, d]() {
                emit reasoningReceived(d);
            }, Qt::QueuedConnection);
        };

        cb.on_tool_call_start = [this]() {
            QMetaObject::invokeMethod(this, [this]() {
                setStatus(QStringLiteral("调用工具中..."));
            }, Qt::QueuedConnection);
        };

        cb.on_tool_start = [this](const std::string& name) {
            QString n = QString::fromStdString(name);
            QMetaObject::invokeMethod(this, [this, n]() {
                setStatus(QStringLiteral("调用工具: ") + n);
                emit toolCallStarted(n);
            }, Qt::QueuedConnection);
        };

        cb.on_tool_finish = [this](const std::string& name, bool ok) {
            QString n = QString::fromStdString(name);
            QMetaObject::invokeMethod(this, [this, n, ok]() {
                emit toolCallFinished(n, ok);
            }, Qt::QueuedConnection);
        };

        cb.on_error = [this](const std::string& error) {
            QString e = QString::fromStdString(error);
            QMetaObject::invokeMethod(this, [this, e]() {
                emit errorOccurred(e);
            }, Qt::QueuedConnection);
        };

        try {
            auto response = app_->agent().process(input, std::move(cb));

            QString fullText = QString::fromStdString(response.content);
            QString finishReason = QString::fromStdString(response.finish_reason);

            QMetaObject::invokeMethod(this, [this, fullText, finishReason]() {
                emit responseComplete(fullText);
                emit usageChanged();
                emit chaptersChanged();
                emit sessionsChanged();  // 首轮对话后会话标题可能已自动提取
                emit skillsChanged(); // save_skill 可能新增了技能
                if (finishReason == "cancelled") {
                    setStatus(QStringLiteral("已取消"));
                } else if (finishReason == "context_overflow") {
                    // 轮内溢出的优雅终止走正常返回路径而非异常，不会进 catch；
                    // 若不在此显式提示，用户只会看到回答无声中断
                    setStatus(QStringLiteral("上下文溢出"));
                    emit errorOccurred(QStringLiteral(
                        "上下文已超限，本轮工具调用被提前终止，请压缩旧对话或开启新会话。"));
                } else {
                    setStatus(QStringLiteral("就绪"));
                }
            }, Qt::QueuedConnection);

        } catch (const std::exception& e) {
            QString err = QString::fromUtf8(e.what());
            QMetaObject::invokeMethod(this, [this, err]() {
                emit errorOccurred(err);
                setStatus(QStringLiteral("错误"));
            }, Qt::QueuedConnection);
        }

        // 响应完成后自动增量索引：内容哈希未变的源会被跳过，
        // 无变更时开销仅为哈希比对，不产生嵌入请求。
        if (project_ && !project_->path.empty() && !cancel_requested_.load())
            runIndexUpdate(/*force=*/false);

        QMetaObject::invokeMethod(this, [this]() {
            busy_.store(false);
            emit busyChanged();
        }, Qt::QueuedConnection);
    });
}

void QmlBridge::runIndexUpdate(bool force) {
    try {
        // 仅手动重建时上报进度；自动增量索引静默执行，不覆盖“就绪”状态
        std::function<void(const std::string&)> progress;
        if (force) {
            progress = [this](const std::string& msg) {
                QString m = QString::fromStdString(msg);
                QMetaObject::invokeMethod(this, [this, m]() {
                    setStatus(m);
                }, Qt::QueuedConnection);
            };
        }
        auto result = app_->indexService()->indexAll(progress, force);
        if (!result.ok()) {
            spdlog::warn("[QmlBridge] 索引更新失败: {}", result.error);
            if (force) {
                QString e = QString::fromStdString(result.error);
                QMetaObject::invokeMethod(this, [this, e]() {
                    emit errorOccurred(QStringLiteral("索引重建失败: ") + e);
                    setStatus(QStringLiteral("索引重建失败"));
                }, Qt::QueuedConnection);
            }
            return;
        }
        spdlog::info("[QmlBridge] 索引更新完成: 更新 {} / 跳过 {} / 清理 {}",
                     result.updated_sources, result.skipped_sources,
                     result.removed_sources);
        if (force) {
            QMetaObject::invokeMethod(this, [this]() {
                setStatus(QStringLiteral("索引重建完成"));
            }, Qt::QueuedConnection);
        }
    } catch (const std::exception& e) {
        spdlog::warn("[QmlBridge] 索引更新异常（不影响会话）: {}", e.what());
    }
}

// ── Provider 配置 ──

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
    // 沿用当前项目（可为 nullptr，即“无项目”状态）
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

// ── 项目操作 / 自动启动 ──

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

    // 上次项目仍有效则自动恢复；无效则以“无项目”状态启动
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

} // namespace qtui
