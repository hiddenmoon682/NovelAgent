// QmlBridge 实现 — 拥有 NovelAgentApp 生命周期 + 串行模式线程安全桥接。

#include "novelagent_qt/QmlBridge.h"

#include "NovelAgentApp.h"
#include "agent/index/IIndexService.h"
#include "agent/session/SessionPersistence.h"
#include "project/Models/Project.h"
#include "project/ProjectIO.h"
#include "project/ProjectManager.h"
#include "utils/FileUtils.h"

#include <QDateTime>
#include <QMetaObject>
#include <QRegularExpression>
#include <QTimeZone>
#include <QUrl>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>

namespace qtui {

namespace {
namespace fu = utils::file;

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
    , alive_(std::make_shared<std::atomic<bool>>(true))
    , indexing_(std::make_shared<std::atomic<bool>>(false))
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
        // 方案 A：先取消全部 in-flight 会话并等待其退场，确保后台池线程的 on_complete
        // 回调在本对象与 app_ 仍存活时执行完毕，避免 use-after-free（成员逆序析构，
        // cancel_requested_ 先于 app_ 销毁，而 ThreadPool 深处 join 会执行残留回调）。
        app_->agent().shutdown();
    }
    // 生命周期令牌复位：即使有超时残留任务，on_complete 的 weak_ptr lock() 也会失败并跳过，
    // 不再访问已进入析构的本对象（兜底 A 的 2s 超时窗口）。
    alive_->store(false);
    // 无界等待自动索引退场：池线程池（std::jthread）析构时本就 join 到任务
    // 结束，此等待只是提前到依赖对象（embedding_gen_/index_service_/sqlite_）
    // 销毁之前，不增加总关闭时长。进度回调的 alive 检查保证 HTTP 返回后
    // 立即在阶段检查点抛"已取消"，不会死等。
    while (indexing_ && indexing_->load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    joinWorker();
}

bool QmlBridge::busy() const {
    // 聚合信号（D12/阶段 4）：索引进行中或任一会话运行即为 true。
    return (indexing_ && indexing_->load()) || (app_ && app_->agent().anyRunning());
}

void QmlBridge::joinWorker() {
    if (worker_.joinable())
        worker_.join();
}

bool QmlBridge::rebuildApp(const std::string& providerName,
                           std::shared_ptr<Project> project, QString* error) {
    if (busy()) {
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

    // 先销毁旧实例，再构造新实例（同 provider 重建）
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

    // 会话焦点复位：重建后旧会话 id 已失效（池已销毁），不清除会导致
    // 首条消息携带陈旧 id 走 sendMessageToSession 报"会话不存在"。
    current_session_id_.clear();
    recent_sessions_.clear();

    // 启动/打开项目时恢复最近会话（对齐 AgentPanel"启动恢复上次对话"注释）：
    // P8 懒物化改版时把"启动即恢复上次对话"一并去掉了，只剩点侧栏才物化——
    // 与界面注释及用户预期不符（启动聊天区空白）。此处把最近使用的历史会话
    // 物化进池并设为当前，随后 agentReadyChanged 触发 AgentPanel 加载其历史。
    // 注意：必须在 current_session_id_.clear() 之后、发信号之前执行。
    {
        if (auto* persistence = app_->agent().persistence()) {
            try {
                const auto sessions = persistence->listSessions();  // updated_at 降序
                if (!sessions.empty()
                    && app_->agent().materializeSession(sessions.front().id)) {
                    current_session_id_ = QString::fromStdString(sessions.front().id);
                    recent_sessions_.append(current_session_id_);
                }
            } catch (const std::exception& e) {
                // 恢复失败不阻塞启动：保持空会话态，由用户点侧栏恢复
                spdlog::warn("[QmlBridge] 启动恢复最近会话失败: {}", e.what());
            }
        }
    }

    emit agentReadyChanged();
    emit projectChanged();
    emit chaptersChanged();
    emit modelChanged();
    emit usageChanged();
    emit skillsChanged();
    emit currentSessionIdChanged();
    setStatus(QStringLiteral("就绪"));
    return true;
}

std::string QmlBridge::activeProviderName() const {
    if (app_) return app_->providerName();
    return config_.default_provider;
}

// ── 属性读取 ──

QString QmlBridge::projectName() const {
    if (app_ && app_->projectAccess() && !app_->projectAccess()->title().empty())
        return QString::fromStdString(app_->projectAccess()->title());
    return QStringLiteral("未打开项目");
}

QString QmlBridge::projectPath() const {
    if (app_ && app_->projectAccess())
        return QString::fromStdString(app_->projectAccess()->path());
    return {};
}

QString QmlBridge::modelName() const {
    if (app_) return QString::fromStdString(app_->modelName());
    if (const auto* p = config_.getDefaultProvider())
        return QString::fromStdString(p->model);
    return {};
}

QString QmlBridge::providerName() const {
    return QString::fromStdString(activeProviderName());
}

bool QmlBridge::sessionBusy() const {
    if (!app_ || current_session_id_.isEmpty()) return false;
    auto* rt = app_->agent().session(current_session_id_.toStdString());
    return rt && rt->running();
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
        emit errorOccurred(QString(), QStringLiteral("尚未完成初始化，请先在设置中配置模型"));
        return;
    }
    // 无当前会话则先建一个多会话池会话（阶段 4）
    if (current_session_id_.isEmpty()) {
        const QString sid = createPoolSession();
        if (sid.isEmpty()) return;
    }
    sendMessageToSession(current_session_id_, text);
}

// ── 多会话并行（阶段 4）──

QString QmlBridge::createPoolSession() {
    if (!app_) return {};
    // 从已有会话视图「+ 新建」（current_session_id_ 非空）时，聊天区必须切到新会话
    // （空历史）——此前漏发 sessionReset，AgentPanel 不重载，对话页仍显示旧会话内容，
    // 且新会话已置 active 使侧栏点击被守卫跳过，表现为"点了没反应"。
    // 首条消息自动建会话（current_session_id_ 为空）时不可重载：sendCurrentMessage
    // 已本地追加用户气泡/回复占位，sessionReset 会清空它们导致用户消息消失。
    const bool had_focus = !current_session_id_.isEmpty();
    const auto id = app_->agent().createSession();
    // 同步池当前焦点：cancelRequest 等按池焦点定位会话，不同步会导致新建会话后取消按钮失效。
    app_->agent().switchSession(id);
    current_session_id_ = QString::fromStdString(id);
    recent_sessions_.removeAll(current_session_id_);
    recent_sessions_.prepend(current_session_id_);  // 最近使用置顶（B2/D3）
    if (had_focus) {
        emit sessionReset();          // 聊天区切到新会话（空历史）
        emit sessionBusyChanged();    // 当前视图会话已变：发送/取消按钮状态需重求值
    }
    emit currentSessionIdChanged();
    emit sessionsChanged();
    return current_session_id_;
}

bool QmlBridge::switchPoolSession(const QString& sessionId) {
    if (!app_) return false;
    // P8 懒物化：点开会话才物化（已在池则切焦点；持久层历史会话则建 runtime+恢复历史）
    if (!app_->agent().materializeSession(sessionId.toStdString())) {
        // 物化失败（DB 异常/会话已归档等）不得静默：按 UI 错误通道就地提示
        spdlog::warn("[QmlBridge] 物化会话失败: {}", sessionId.toStdString());
        emit uiErrorOccurred(QStringLiteral("会话打开失败（可能已被删除）"));
        return false;
    }
    current_session_id_ = sessionId;
    // 注意：切换（查看）会话不得改变"最近活动"排序——列表只按发送/创建时间排序。
    // 物化路径已把 runtime 排序时间戳恢复为库内真实值（见 SessionPool::materializeSession），
    // 此处不再 prepend recent_sessions_（recent_sessions_ 仅用于删除当前会话时的焦点回退，
    // 按"最近发送/创建"语义维护，查看不改变它）。
    app_->agent().releaseIdleClients();   // D6：切走释放非运行会话的 client 连接
    emit currentSessionIdChanged();
    emit sessionsChanged();       // 切会话后侧栏 active 高亮/最近使用排序需即时刷新
    emit sessionBusyChanged();
    emit sessionReset();
    return true;
}

void QmlBridge::sendMessageToSession(const QString& sessionId, const QString& text) {
    if (text.trimmed().isEmpty()) return;
    if (!app_) return;
    auto* rt = app_->agent().session(sessionId.toStdString());
    if (!rt) {
        emit errorOccurred(sessionId, QStringLiteral("会话不存在"));
        return;
    }
    // 按目标会话自身运行态检查（支持多会话并行：不同会话可同时提交）
    if (rt->running()) {
        spdlog::warn("[QmlBridge] 会话 {} 正在生成中，忽略重复请求", sessionId.toStdString());
        return;
    }
    // 发送即视为最近使用：立即刷新 runtime 排序时间戳（列表即刻置顶；
    // 每轮完成的 saveSessionState 还会再刷新一次并写库，重启后顺序一致）。
    rt->touchActivity();
    recent_sessions_.removeAll(sessionId);
    recent_sessions_.prepend(sessionId);  // 发消息即视为最近使用（B2/D3）
    emit sessionsChanged();               // 提交时即刷新侧栏顺序（不必等 responseComplete）
    runAgent(sessionId.toStdString(), text.toStdString());
}

bool QmlBridge::deletePoolSession(const QString& sessionId) {
    if (!app_) return false;
    const std::string sid = sessionId.toStdString();
    // 删除前先判定持久层是否已有该会话行（含归档），用于区分"从未落盘"（无行，
    // 可直接静默成功）与"已落盘但归档失败"（真失败，重启后可能复活，必须提示）。
    // 新建后未跑完一轮消息的会话不会产生 DB 行，此时 deleteSession 必然返回 false
    // ——不能把"行不存在"当成"归档失败"弹吓人错误（历史假警报）。
    bool has_row = false;
    if (auto* p = app_->agent().persistence()) {
        try {
            has_row = p->sessionIdExists(sid);
        } catch (const std::exception& e) {
            spdlog::warn("[QmlBridge] 删除前查会话行失败: {}", e.what());
        }
    }
    // 先等运行中会话退出并移除内存池，再从持久层删除（避免运行时读到写到一半的数据）
    if (!app_->agent().deleteSessionRuntime(sid)) return false;
    // 删除持久层会话：novel.db 的 sessions 表置 archived=1（数据保留、列表不可见），
    // 否则已删池会话重启后仍会从持久层复活。未打开项目（无持久化）时跳过。
    if (auto* persistence = app_->agent().persistence()) {
        if (has_row) {
            try {
                if (!persistence->deleteSession(sid)) {
                    // 归档失败（如行不存在/库异常）：runtime 已移除，重启后该会话会复活
                    spdlog::warn("[QmlBridge] 持久层归档会话 {} 失败（重启后可能复活）", sid);
                    emit uiErrorOccurred(QStringLiteral("会话已从列表移除，但归档失败（重启后可能恢复显示）"));
                }
            } catch (const std::exception& e) {
                spdlog::warn("[QmlBridge] 归档会话 {} 异常: {}", sid, e.what());
                emit uiErrorOccurred(QStringLiteral("会话归档失败: ")
                                     + QString::fromUtf8(e.what()));
            }
        }
        // 从未落盘（无行）：无可归档内容，静默成功——删除即彻底移除，无"复活"风险
    }
    // 清理最近使用记录中的已删 id，避免其无限增长
    recent_sessions_.removeAll(sessionId);
    const bool was_current = (current_session_id_ == sessionId);
    if (was_current) {
        // 焦点回退：优先取最近使用（recent_sessions_）仍在池的会话，其次池首项。
        // 此前取 map 首项（最早创建，字典序），与"自动切到最近会话"的注释不符。
        const auto ids = app_->agent().sessionIds();
        QString fallback;
        for (const auto& sid : recent_sessions_) {
            if (std::find(ids.begin(), ids.end(), sid.toStdString()) != ids.end()) {
                fallback = sid;
                break;
            }
        }
        if (fallback.isEmpty() && !ids.empty())
            fallback = QString::fromStdString(ids.front());
        // 重同步池焦点：SessionPool 的 current_session_id_ 仍指向已删 id（deleteSessionRuntime
        // 只移除不改焦点），若不切走，cancelRequest 等按池焦点路由的操作会经 currentSession()
        // 自动新建一个用户从未见过的幽灵会话（历史 bug 根因之一）。
        if (!fallback.isEmpty())
            app_->agent().switchSession(fallback.toStdString());
        current_session_id_ = fallback;
        emit currentSessionIdChanged();
        emit sessionReset();        // 仅删除当前会话才重置聊天流；删非当前不影响正在观看的会话
        emit sessionBusyChanged();  // 当前视图会话已变：发送/取消按钮状态重新求值
    }
    emit sessionsChanged();
    return true;
}

void QmlBridge::cancelRequest() {
    // 取消当前查看会话（仅当其在运行；空闲会话无取消按钮，不会走到这里）。
    // 以池当前会话为准，避免 current_session_id_ 与池焦点失同步时误取消其它会话。
    if (!app_) return;
    // 空池早退：currentSessionId() 在无会话时会自动新建会话（只读语义污染
    // 会造出用户从未见过的幽灵会话）；取消按钮只在当前会话运行中才可见，空池无意义。
    if (app_->agent().sessionIds().empty()) return;
    auto* rt = app_->agent().session(app_->agent().currentSessionId());
    if (rt && rt->running()) {
        app_->agent().requestCancel();
        setStatus(QStringLiteral("正在取消..."));
    }
}

QVariantList QmlBridge::sessionList() const {
    QVariantList list;
    if (!app_) return list;

    // 统一按"最近活动时间"降序合并展示（池会话 + 持久层历史会话），保证
    // "按最近使用降序"贯穿整个列表：池会话取 runtime 最近活动时间（创建/每轮
    // 完成刷新），持久层历史会话取 DB updated_at（UTC "yyyyMMddTHHmmssZ"）。
    struct Entry {
        QString id;
        QString title;    // 持久层条目的 DB 标题；池条目为空、发射时从内存取
        qint64 updated_ms;
        bool in_pool;
    };
    std::vector<Entry> ordered;

    const auto pool_ids = app_->agent().sessionIds();
    for (const auto& sid : pool_ids) {
        auto* rt = app_->agent().session(sid);
        if (!rt) continue;
        ordered.push_back({QString::fromStdString(sid), QString(),
                           rt->updatedAtMs(), true});
    }

    // 持久层历史会话（未物化）：合并展示，点开时经 switchPoolSession 物化。
    auto* persistence = app_->agent().persistence();
    if (persistence) {
        // 持久层异常（磁盘 IO/重建失败）不得穿透 Q_INVOKABLE 进 QML 引擎
        try {
            for (const auto& s : persistence->listSessions()) {
                if (std::find(pool_ids.begin(), pool_ids.end(), s.id) != pool_ids.end())
                    continue;  // 已在池的跳过（上方池条目已含）
                qint64 ms = 0;
                // 库内时间戳为 UTC "yyyy-MM-ddTHH:mm:ssZ"（ProjectIO::nowTimestamp 产出，
                // 秒级精度）；此前按紧凑格式解析全部失败（ms=0）致排序退化。
                // Qt 6.8 弃用 setTimeSpec，改用 QTimeZone
                QDateTime dt = QDateTime::fromString(
                    QString::fromStdString(s.updated_at),
                    QStringLiteral("yyyy-MM-ddTHH:mm:ss'Z'"));
                if (dt.isValid()) {
                    dt.setTimeZone(QTimeZone::UTC);
                    ms = dt.toMSecsSinceEpoch();
                }
                ordered.push_back({QString::fromStdString(s.id),
                                   s.title.empty() ? QStringLiteral("新会话")
                                                   : QString::fromStdString(s.title),
                                   ms, false});
            }
        } catch (const std::exception& e) {
            spdlog::warn("[QmlBridge] 读取会话列表失败: {}", e.what());
        }
    }

    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const Entry& a, const Entry& b) {
                         return a.updated_ms > b.updated_ms;
                     });

    for (const auto& e : ordered) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), e.id);
        m.insert(QStringLiteral("active"), current_session_id_ == e.id);
        if (e.in_pool) {
            auto* rt = app_->agent().session(e.id.toStdString());
            if (!rt) continue;
            QString title = QStringLiteral("新会话");
            // GUI 线程跨线程读运行中会话的 memory：必须走加锁快照（messages() 裸引用
            // 与池线程的 vector 变异并发迭代是数据竞争）
            for (const auto& msg : rt->memory().snapshot()) {
                if (msg.role != llm::MessageRole::User) continue;
                QString c = QString::fromStdString(msg.content);
                c = c.section('\n', 0, 0);
                if (c.size() > 30) c = c.left(30) + QStringLiteral("…");
                title = c;
                break;
            }
            m.insert(QStringLiteral("title"), title);
            m.insert(QStringLiteral("running"), rt->running());
        } else {
            m.insert(QStringLiteral("title"), e.title);
            m.insert(QStringLiteral("running"), false);
        }
        m.insert(QStringLiteral("updatedAt"), e.updated_ms > 0
                     ? QDateTime::fromMSecsSinceEpoch(e.updated_ms, QTimeZone::UTC)
                           .toString(QStringLiteral("yyyyMMddTHHmmss'Z'"))
                     : QString());
        list.push_back(m);
    }

    // 未落盘的新会话（persisted==false）已在池会话区按真实 id 展示，不再插入
    // 空 id 占位：旧逻辑同时显示"占位 + 真实会话"两项，删除占位会经
    // discardPendingNewSession 连真实会话一起删掉（用户感知"删一个丢两个"）。
    return list;
}

bool QmlBridge::deleteSession(const QString& sessionId) {
    if (!app_) return false;
    if (busy()) {
        // 生成/索引进行中删除会话会走失败路径且无反馈；按 UI 错误通道就地提示
        // （对齐删除项目的既有做法），避免用户以为删除成功。
        emit uiErrorOccurred(QStringLiteral("Agent 正在生成中，请稍后再删除会话"));
        return false;
    }
    // 多会话池会话：走池删除（阶段 4）
    if (!sessionId.isEmpty() && app_->agent().session(sessionId.toStdString()) != nullptr)
        return deletePoolSession(sessionId);
    // 删除"未落盘新会话"占位（ID 为空）：直接丢弃 pending，不落盘
    if (sessionId.isEmpty()) {
        if (!app_->agent().discardPendingNewSession()) return false;
        emit sessionsChanged();
        emit sessionReset();
        emit usageChanged();
        setStatus(QStringLiteral("已丢弃未发送的新会话"));
        return true;
    }
    // 持久层历史会话（不在池）：直接删除持久层记录（置 archived=1，数据保留、列表不可见）
    auto* persistence = app_->agent().persistence();
    const bool wasCurrent = (current_session_id_ == sessionId);
    try {
        // DB 异常不得穿透 Q_INVOKABLE（与物化路径同型防护）：失败按常规 false 返回并由调用方提示
        if (!persistence || !persistence->deleteSession(sessionId.toStdString())) {
            if (persistence)
                emit uiErrorOccurred(QStringLiteral("会话删除失败（可能已被删除）"));
            return false;
        }
    } catch (const std::exception& e) {
        spdlog::warn("[QmlBridge] 删除持久层会话 {} 异常: {}", sessionId.toStdString(), e.what());
        emit uiErrorOccurred(QStringLiteral("会话删除失败: ") + QString::fromUtf8(e.what()));
        return false;
    }
    if (wasCurrent) current_session_id_.clear();
    emit sessionsChanged();
    if (wasCurrent) {
        emit sessionReset();
        emit usageChanged();
    }
    setStatus(QStringLiteral("会话已删除（数据保留、列表不可见）"));
    return true;
}

QVariantList QmlBridge::conversationHistory() const {
    QVariantList list;
    if (!app_) return list;

    // 多会话并行：优先读当前池会话的独立内存；否则回退单会话 active 内存。
    // GUI 线程跨线程读取：走 IMemory::snapshot() 加锁快照，避免与池线程的
    // memory 变异（inject/apply，vector 重分配）并发迭代导致数据竞争。
    const llm::IMemory* mem = nullptr;
    if (!current_session_id_.isEmpty()) {
        auto* rt = app_->agent().session(current_session_id_.toStdString());
        if (rt) mem = &rt->memory();
    }
    if (!mem) mem = &app_->agent().memory();

    for (const auto& msg : mem->snapshot()) {
        const bool isUser = msg.role == llm::MessageRole::User;
        const bool isAssistant = msg.role == llm::MessageRole::Assistant;
        if (!isUser && !isAssistant) continue;          // 跳过 tool 结果消息
        if (msg.content.empty()) continue;              // 跳过纯 tool_calls 占位消息
        if (msg.is_control) continue;                   // P6：控制消息（取消占位）不显示
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
        emit uiErrorOccurred(QStringLiteral("尚未完成初始化，无法重建索引"));
        return;
    }
    if (!app_->projectAccess() || app_->projectAccess()->path().empty()) {
        emit uiErrorOccurred(QStringLiteral("未打开项目，无法重建索引"));
        return;
    }
    if (busy()) {
        spdlog::warn("[QmlBridge] 忽略重建索引请求（会话生成中或索引进行中）");
        return;
    }

    joinWorker();
    // 捕获共享标志副本：worker 线程（含本对象析构后）写清标志仍安全
    auto flag = indexing_;
    flag->store(true);
    emit busyChanged();
    setStatus(QStringLiteral("正在重建索引..."));

    worker_ = std::thread([this, flag]() {
        runIndexUpdate(/*force=*/true);
        QMetaObject::invokeMethod(this, [this, flag]() {
            flag->store(false);
            emit busyChanged();
        }, Qt::QueuedConnection);
    });
}

QVariantList QmlBridge::chapterList() const {
    QVariantList list;
    if (!app_ || !app_->projectAccess()) return list;

    // 锁内读取章节元数据（GUI 线程与池线程工具并发，防撕裂读）
    app_->projectAccess()->withReadLock([&](const Project& p) {
        std::vector<const Chapter*> sorted;
        sorted.reserve(p.outline.chapters.size());
        for (const auto& ch : p.outline.chapters)
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
    });
    return list;
}

QString QmlBridge::loadChapter(const QString& chapterId) {
    if (!app_ || !app_->projectAccess()) return {};
    const std::string id = chapterId.toStdString();

    // 锁内取章节元数据（GUI 线程与池线程工具并发，防撕裂读）；文件 IO 在锁外
    std::string file_path;
    bool found = false;
    app_->projectAccess()->withReadLock([&](const Project& p) {
        for (const auto& ch : p.outline.chapters) {
            if (ch.id != id) continue;
            file_path = ch.file_path;
            found = true;
            return;
        }
    });
    if (!found) {
        emit uiErrorOccurred(QStringLiteral("未找到章节: ") + chapterId);
        return {};
    }
    if (file_path.empty()) {
        emit uiErrorOccurred(QStringLiteral("章节尚未写入正文文件"));
        return {};
    }
    const std::string content = ProjectIO::readChapter(
        app_->projectAccess()->path(), file_path);
    if (content.empty())
        emit uiErrorOccurred(QStringLiteral("章节文件不存在或为空: ")
                             + QString::fromStdString(file_path));
    return QString::fromStdString(content);
}

// ── 技能管理 ──

QVariantList QmlBridge::skillList() const {
    QVariantList list;
    if (!app_) return list;

    for (const auto& s : app_->skillRegistry().listSkills()) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), QString::fromStdString(s.name));
        m.insert(QStringLiteral("description"), QString::fromStdString(s.description));
        m.insert(QStringLiteral("always"), s.always);
        m.insert(QStringLiteral("enabled"), s.enabled);
        list.push_back(m);
    }
    return list;
}

bool QmlBridge::setSkillEnabled(const QString& name, bool enabled) {
    if (!app_) return false;
    if (busy()) {
        emit uiErrorOccurred(QStringLiteral("生成中无法切换技能，请稍后再试"));
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

void QmlBridge::runAgent(const std::string& session_id, std::string input) {
    cancel_requested_.store(false);
    emit busyChanged();  // 进入运行态：busy() 聚合到 anyRunning，此处通知 QML 重新求值
    emit sessionBusyChanged();  // 当前会话进入运行态（按会话 busy）
    setStatus(QStringLiteral("思考中..."));

    const QString sid = QString::fromStdString(session_id);
    llm::StreamCallbacks cb;

    cb.on_content = [this, sid](const std::string& delta) {
        QString d = QString::fromStdString(delta);
        QMetaObject::invokeMethod(this, [this, sid, d]() {
            emit tokenReceived(sid, d);
        }, Qt::QueuedConnection);
    };

    cb.on_reasoning = [this, sid](const std::string& delta) {
        QString d = QString::fromStdString(delta);
        QMetaObject::invokeMethod(this, [this, sid, d]() {
            emit reasoningReceived(sid, d);
        }, Qt::QueuedConnection);
    };

    cb.on_tool_call_start = [this]() {
        QMetaObject::invokeMethod(this, [this]() {
            setStatus(QStringLiteral("调用工具中..."));
        }, Qt::QueuedConnection);
    };

    cb.on_tool_start = [this, sid](const std::string& name) {
        QString n = QString::fromStdString(name);
        QMetaObject::invokeMethod(this, [this, sid, n]() {
            setStatus(QStringLiteral("调用工具: ") + n);
            emit toolCallStarted(sid, n);
        }, Qt::QueuedConnection);
    };

    cb.on_tool_finish = [this, sid](const std::string& name, bool ok) {
        QString n = QString::fromStdString(name);
        QMetaObject::invokeMethod(this, [this, sid, n, ok]() {
            emit toolCallFinished(sid, n, ok);
        }, Qt::QueuedConnection);
    };

    cb.on_error = [this, sid](const std::string& error) {
        QString e = QString::fromStdString(error);
        QMetaObject::invokeMethod(this, [this, sid, e]() {
            emit errorOccurred(sid, e);  // 带会话维度：仅正在查看该会话时展示
        }, Qt::QueuedConnection);
    };

    // P1 异步：提交共享线程池立即返回，完成回调在池线程调用
    //（回调内部再 QueuedConnection 投递到 GUI 线程发射信号）。
    app_->agent().submitProcess(
        session_id, std::move(input), std::move(cb),
        [this, sid, alive = std::weak_ptr<std::atomic<bool>>(alive_)](
            const std::string&, llm::LLMResponse response) {
            // 生命周期令牌：本对象已析构（alive_ 复位）则 lock 失败即跳过，兜底超时残留，
            // 确保池线程不访问已析构的 this（方案 B）。
            if (!alive.lock()) return;
            const QString fullText = QString::fromStdString(response.content);
            const QString finishReason = QString::fromStdString(response.finish_reason);

            // 本轮被拒绝/取消（无新增内容）的响应不触发自动增量索引：
            // 此前取消/并发满/会话不存在等轮也会空跑 indexAll，纯浪费
            // 且拒绝路径可能阻塞 GUI 线程（评审 I3/M1）。
            const bool rejected_round =
                finishReason == QStringLiteral("cancelled") ||
                finishReason == QStringLiteral("concurrency_full") ||
                finishReason == QStringLiteral("session_busy") ||
                finishReason == QStringLiteral("session_not_found");

            // 池线程：响应完成后自动增量索引（内容哈希未变的源会被跳过；
            // 无变更时开销仅为哈希比对，不产生嵌入请求）。
            if (app_ && app_->projectAccess()
                && !app_->projectAccess()->path().empty()
                && !cancel_requested_.load()
                && !rejected_round
                && alive.lock()) {
                auto flag = indexing_;
                flag->store(true);
                QMetaObject::invokeMethod(this, [this]() { emit busyChanged(); },
                                          Qt::QueuedConnection);
                try {
                    // 进度回调仅捕获 alive 弱引用（不触碰 this）：应用析构后
                    // indexAll 在阶段检查点抛"已取消"，安全中止，不访问悬垂成员
                    std::function<void(const std::string&)> cancel_progress =
                        [alive](const std::string&) {
                            if (!alive.lock())
                                throw std::runtime_error("索引已取消：应用正在关闭");
                        };
                    auto idx_result = app_->indexService()->indexAll(cancel_progress, /*force=*/false);
                    if (!idx_result.ok()) {
                        spdlog::warn("[QmlBridge] 自动索引更新失败: {}", idx_result.error);
                        // 仅记日志时 GUI 无感知（状态仍"就绪"、搜索静默为空）；
                        // 应用仍存活则经 errorOccurred 上报一次，关闭导致的取消不打扰用户
                        if (alive.lock()) {
                            QString e = QString::fromStdString(idx_result.error);
                            QMetaObject::invokeMethod(this, [this, e]() {
                                emit errorOccurred(QString(), QStringLiteral("自动索引更新失败: ") + e);
                            }, Qt::QueuedConnection);
                        }
                    }
                } catch (const std::exception& e) {
                    spdlog::warn("[QmlBridge] 自动索引取消/失败: {}", e.what());
                    // 关闭/切换导致的取消（alive 失效）不打扰用户；其余异常同样上报一次
                    if (alive.lock()) {
                        QString msg = QString::fromStdString(e.what());
                        QMetaObject::invokeMethod(this, [this, msg]() {
                            emit errorOccurred(QString(), QStringLiteral("自动索引更新异常: ") + msg);
                        }, Qt::QueuedConnection);
                    }
                }
                flag->store(false);
                QMetaObject::invokeMethod(this, [this]() { emit busyChanged(); },
                                          Qt::QueuedConnection);
            }

            QMetaObject::invokeMethod(this, [this, sid, fullText, finishReason]() {
                emit responseComplete(sid, fullText);
                emit usageChanged();
                emit chaptersChanged();
                emit sessionsChanged();  // 首轮对话后会话标题可能已自动提取
                emit skillsChanged(); // save_skill 可能新增了技能
                if (finishReason == "cancelled") {
                    setStatus(QStringLiteral("已取消"));
                } else if (finishReason == "concurrency_full") {
                    // P9：并发上限拒绝（非正常完成，无内容）
                    setStatus(QStringLiteral("并发已满"));
                    emit errorOccurred(sid, QStringLiteral(
                        "当前并发会话已满（上限 4），请稍后再试。"));
                } else if (finishReason == "session_busy") {
                    // 同会话重复提交被池拒绝（防双提交竞态），静默复位即可
                    setStatus(QStringLiteral("就绪"));
                } else if (finishReason == "context_overflow") {
                    // 轮内溢出的优雅终止走正常返回路径而非异常，不会进 catch；
                    // 若不在此显式提示，用户只会看到回答无声中断
                    setStatus(QStringLiteral("上下文溢出"));
                    emit errorOccurred(sid, QStringLiteral(
                        "上下文已超限，本轮工具调用被提前终止，请压缩旧对话或开启新会话。"));
                } else {
                    setStatus(QStringLiteral("就绪"));
                }
                // 退出运行态：busy() 聚合到 anyRunning，此处通知 QML 重新求值
                emit busyChanged();
                emit sessionBusyChanged();  // 当前会话退出运行态（按会话 busy）
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
                    emit uiErrorOccurred(QStringLiteral("索引重建失败: ") + e);
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
    // isDefault 用 provider 标识（map 键，即入参 name）判断，而非 p->name：
    // p->name 承载"显示名（命名）"，可与标识不同；用标识判默认才稳定。
    m.insert(QStringLiteral("isDefault"), config_.default_provider == name.toStdString());
    return m;
}

bool QmlBridge::saveProvider(const QString& name, const QVariantMap& v) {
    const std::string key = name.toStdString();
    if (key.empty()) return false;
    if (!config_.providers.count(key)) return false;  // 仅保存已存在的 provider

    ProviderConfig& p = config_.providers[key];

    // 命名 = 显示名，只更新 p->name，不再做 map 键迁移。
    // WHY：对齐原型「Provider 标识（map 键）只读稳定、命名可改显示名」，
    //      避免重命名连带改动 default_provider 引用与运行中 Agent 的 provider 名。
    const QString renameTo = v.value(QStringLiteral("rename_to")).toString().trimmed();
    if (!renameTo.isEmpty())
        p.name = renameTo.toStdString();

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

QString QmlBridge::addProvider() {
    // 自动生成唯一名："未命名"、"未命名-2"、"未命名-3"...
    std::string name = "未命名";
    int n = 2;
    while (config_.providers.count(name))
        name = "未命名-" + std::to_string(n++);
    config_.providers[name] = ProviderConfig{};
    config_.providers[name].name = name;
    config_.save();
    emit providersChanged();
    return QString::fromStdString(name);
}

bool QmlBridge::deleteProvider(const QString& name) {
    const std::string key = name.toStdString();
    if (key.empty() || !config_.providers.count(key))
        return false;
    if (key == config_.default_provider) {
        emit uiErrorOccurred(QStringLiteral("默认模型不能删除，请先切换默认模型"));
        return false;
    }
    if (app_ && key == activeProviderName()) {
        emit uiErrorOccurred(QStringLiteral("当前正在使用的模型不能删除"));
        return false;
    }
    config_.providers.erase(key);
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
        emit uiErrorOccurred(err);
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
        emit uiErrorOccurred(QStringLiteral("无法打开项目（目录中缺少有效的 novel.json）: ")
                             + QString::fromStdString(dir));
        return false;
    }
    QString err;
    if (!rebuildApp(activeProviderName(),
                    std::make_shared<Project>(std::move(p)), &err)) {
        emit uiErrorOccurred(err);
        return false;
    }
    config_.last_project_path = dir;
    config_.recordRecentProject(dir);
    config_.save();
    return true;
}

bool QmlBridge::createProject(const QString& dirPath, const QString& title) {
    const std::string dir = toLocalPath(dirPath);
    const std::string name = title.trimmed().toStdString();
    if (dir.empty() || name.empty()) {
        emit uiErrorOccurred(QStringLiteral("项目路径和名称不能为空"));
        return false;
    }
    ProjectManager pm;
    Project p = pm.create(dir, name);
    if (p.title.empty()) {
        emit uiErrorOccurred(QStringLiteral("创建项目失败: ") + QString::fromStdString(dir));
        return false;
    }
    QString err;
    if (!rebuildApp(activeProviderName(),
                    std::make_shared<Project>(std::move(p)), &err)) {
        emit uiErrorOccurred(err);
        return false;
    }
    config_.last_project_path = dir;
    config_.recordRecentProject(dir);
    config_.save();
    return true;
}

QVariantList QmlBridge::recentProjects() const {
    QVariantList result;
    const QString current = projectPath();
    for (const auto& dir : config_.recent_projects) {
        const QString path = QString::fromStdString(dir);
        const std::string title = ProjectIO::peekTitle(dir);
        QVariantMap item;
        item.insert(QStringLiteral("title"),
                    title.empty() ? QVariant(path)
                                  : QVariant(QString::fromStdString(title)));
        item.insert(QStringLiteral("path"), path);
        item.insert(QStringLiteral("isCurrent"), path == current);
        result.append(item);
    }
    return result;
}

bool QmlBridge::removeRecentProject(const QString& path) {
    const bool hit = config_.removeRecentProject(toLocalPath(path));
    if (hit) {
        config_.save();
    }
    return hit;
}

QString QmlBridge::lastProjectPath() const {
    return QString::fromStdString(config_.last_project_path);
}

// ── 固定目录项目管理 ──

QString QmlBridge::projectsDir() const {
    return QString::fromStdString(fu::joinPath(fu::configDir(), "projects"));
}

QString QmlBridge::createProjectAt(const QString& title, const QString& description) {
    const std::string name = title.trimmed().toStdString();
    if (name.empty()) return QStringLiteral("invalid_title");
    if (title.trimmed().contains(QRegularExpression(QStringLiteral(R"([\\\/:*?"<>|])"))))
        return QStringLiteral("invalid_chars");

    namespace fs = std::filesystem;
    const std::string base = toLocalPath(projectsDir());
    ProjectManager pm;

    // 重名判定：枚举有效项目 + 读取标题（大小写不敏感）。
    // 软删目录（目录名带"（已删除）"）不参与：它在列表中被过滤、用户不可见，
    // 若仍占着标题会挡住同名再创建（历史 bug：删掉"测试"后无法再建"测试"）。
    for (const auto& dir : pm.listProjects(base)) {
        if (ProjectManager::isSoftDeleted(dir))
            continue;
        std::string t = ProjectIO::peekTitle(dir);
        if (t.empty()) t = fu::baseName(dir);
        std::string a = t, b = name;
        std::transform(a.begin(), a.end(), a.begin(), ::tolower);
        std::transform(b.begin(), b.end(), b.begin(), ::tolower);
        if (a == b) return QStringLiteral("duplicate");
    }

    // 目录名 = 标题安全化；与既有目录冲突时追加 -2、-3...
    std::string dir = fu::joinPath(base, ProjectManager::getDefaultProjectDir(name));
    int suffix = 2;
    std::error_code ec;
    while (fs::exists(dir, ec)) {
        dir = fu::joinPath(base,
               ProjectManager::getDefaultProjectDir(name) + "-" + std::to_string(suffix++));
        ec.clear();
    }

    if (busy()) {
        emit uiErrorOccurred(QStringLiteral("Agent 正在生成中，请稍后再创建项目"));
        return QStringLiteral("failed");
    }
    fu::createDirs(base);
    Project p = pm.create(dir, name, description.trimmed().toStdString());
    if (p.title.empty()) return QStringLiteral("failed");

    QString err;
    if (!rebuildApp(activeProviderName(),
                    std::make_shared<Project>(std::move(p)), &err)) {
        emit uiErrorOccurred(err);
        return QStringLiteral("failed");
    }
    config_.last_project_path = dir;
    config_.recordRecentProject(dir);
    config_.save();
    return QStringLiteral("ok");
}

QVariantList QmlBridge::allProjects() const {
    QVariantList result;
    const QString current = projectPath();
    const std::string base = toLocalPath(projectsDir());
    ProjectManager pm;

    // 枚举固定目录的有效项目，过滤软删目录（目录名带"（已删除）"标记）
    std::vector<std::string> dirs;
    for (const auto& dir : pm.listProjects(base)) {
        if (ProjectManager::isSoftDeleted(dir))
            continue;
        dirs.push_back(dir);
    }

    // 按 recent_projects 中的下标排序（不在 recent 的排最后，保持枚举顺序）
    std::stable_sort(dirs.begin(), dirs.end(),
        [this](const std::string& a, const std::string& b) {
            const auto& rec = config_.recent_projects;
            auto ia = std::find(rec.begin(), rec.end(), a);
            auto ib = std::find(rec.begin(), rec.end(), b);
            if (ia == rec.end() && ib == rec.end()) return false;
            if (ia == rec.end()) return false;
            if (ib == rec.end()) return true;
            return ia < ib;
        });

    for (const auto& dir : dirs) {
        const QString path = QString::fromStdString(dir);
        const std::string title = ProjectIO::peekTitle(dir);
        QVariantMap item;
        item.insert(QStringLiteral("title"),
                    title.empty() ? QVariant(path)
                                  : QVariant(QString::fromStdString(title)));
        item.insert(QStringLiteral("path"), path);
        item.insert(QStringLiteral("isCurrent"), path == current);
        result.append(item);
    }
    return result;
}

bool QmlBridge::deleteProject(const QString& path) {
    const std::string dir = toLocalPath(path);
    if (dir.empty()) return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
        return false;

    const bool isCurrent = (dir == toLocalPath(projectPath()));
    // 删除当前项目：Agent/SqliteStore 持有项目内 novel.db 等文件的句柄，
    // Windows 上重命名含被打开文件的目录会失败——必须**先**重建为无项目状态
    // （释放句柄），再重命名目录（历史 bug：软删一直失败于"目录重命名出错"）。
    if (isCurrent) {
        if (busy()) {
            emit uiErrorOccurred(QStringLiteral("Agent 正在生成中，请稍后再删除"));
            return false;
        }
        QString err;
        if (!rebuildApp(activeProviderName(), nullptr, &err)) {
            emit uiErrorOccurred(err);
            return false;
        }
    }

    // 软删：目录重命名为"原名（已删除）"，目标名冲突时追加 -2、-3...
    std::string renamed;
    std::string candidate = dir + "（已删除）";
    int suffix = 2;
    while (fs::exists(candidate, ec)) {
        candidate = dir + "（已删除）-" + std::to_string(suffix++);
        ec.clear();
    }
    fs::rename(dir, candidate, ec);
    if (ec) {
        emit uiErrorOccurred(QStringLiteral("删除失败（目录重命名出错）: ")
                             + QString::fromStdString(ec.message()));
        return false;
    }
    renamed = candidate;

    // 清 last_project_path 防止下次启动把软删项目复活
    if (config_.last_project_path == dir)
        config_.last_project_path.clear();
    config_.removeRecentProject(dir);
    config_.save();
    return true;
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
