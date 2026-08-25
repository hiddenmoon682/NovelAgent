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
// 线程模型（多会话并行）：
//   - sendMessage() 提交共享线程池异步执行（不阻塞 UI）；流式回调经
//     QMetaObject::invokeMethod(Qt::QueuedConnection) 转发到 QML 主线程。
//   - busy() 为聚合信号：索引重建中或任一会话运行即为 true（全局操作锁）；
//     输入框/发送按钮按当前查看会话的 sessionBusy 控制。
//   - worker_（索引线程）不再 detach；重建/析构前 joinWorker()。

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
    Q_PROPERTY(bool sessionBusy READ sessionBusy NOTIFY sessionBusyChanged)
    Q_PROPERTY(QString modelName READ modelName NOTIFY modelChanged)
    Q_PROPERTY(QString providerName READ providerName NOTIFY modelChanged)
    Q_PROPERTY(int totalTokens READ totalTokens NOTIFY usageChanged)
    Q_PROPERTY(int contextPercent READ contextPercent NOTIFY usageChanged)
    Q_PROPERTY(QString currentSessionId READ currentSessionId NOTIFY currentSessionIdChanged)

public:
    explicit QmlBridge(QObject* parent = nullptr);
    ~QmlBridge() override;

    // ── 属性读取 ──
    bool agentReady() const { return app_ != nullptr; }
    QString projectName() const;
    QString projectPath() const;
    QString statusText() const { return status_text_; }
    // 全局 busy（聚合信号，D12/阶段 4）：索引重建进行中或任一会话运行即为 true。
    // 用于全局操作锁（重建/切技能）与 QML 禁用按钮；输入框按会话 busy 由 sessionBusy 表达。
    bool busy() const;
    // 当前查看会话是否正在生成（按会话 busy，D12/阶段 4）：
    // 该会话在后台跑时用户可切到其它空闲会话发消息。
    bool sessionBusy() const;
    QString modelName() const;
    QString providerName() const;
    int totalTokens() const;
    int contextPercent() const;

    // ── 会话交互（既有）──
    Q_INVOKABLE void sendMessage(const QString& text);
    Q_INVOKABLE void cancelRequest();
    Q_INVOKABLE void newSession();
    // 多会话并行（阶段 4）：当前查看会话 id（pool 会话）。
    QString currentSessionId() const { return current_session_id_; }
    // 新建一个多会话池会话并设为当前，返回其 id（空串 = 未就绪）。
    Q_INVOKABLE QString createPoolSession();
    // 切换到指定 pool 会话（仅切焦点，不阻塞）。
    Q_INVOKABLE bool switchPoolSession(const QString& sessionId);
    // 向指定 pool 会话发送消息（多会话并行路径）。
    Q_INVOKABLE void sendMessageToSession(const QString& sessionId, const QString& text);
    // 删除指定 pool 会话（返回是否存在；删除当前会话后自动切到剩余池会话）。
    Q_INVOKABLE bool deletePoolSession(const QString& sessionId);
    // 会话列表（按最近使用降序）：[{id, title, active, updatedAt}, ...]；未就绪返回空。
    Q_INVOKABLE QVariantList sessionList() const;
    // 切换到指定会话（保存当前会话后重载）；生成中/未就绪返回 false。
    Q_INVOKABLE bool switchSession(const QString& sessionId);
    // 删除指定会话（内容归档到 archive/）；删除 active 会话时自动切换。
    Q_INVOKABLE bool deleteSession(const QString& sessionId);
    // 当前内存中的对话历史（启动恢复后供 QML 重建聊天流）：
    // [{role: "user"|"assistant", content, reasoning}, ...]，跳过工具消息与空消息。
    Q_INVOKABLE QVariantList conversationHistory() const;
    Q_INVOKABLE void refreshProject();
    // 强制全量重建向量索引（清空后重嵌入全部源），后台执行，与自动索引共用 indexing_ 标志互斥。
    Q_INVOKABLE void rebuildIndex();
    // 章节列表（按 order 升序）：[{id, title, order, wordCount}, ...]；项目未打开返回空。
    Q_INVOKABLE QVariantList chapterList() const;
    // 读取章节正文 Markdown；失败返回空串并 emit errorOccurred。
    Q_INVOKABLE QString loadChapter(const QString& chapterId);

    // ── 启动 / 初始化 ──
    // 若默认 provider 已有有效 key 则自动初始化（并恢复上次项目），返回是否成功。
    Q_INVOKABLE bool tryAutoStart();
    // 用指定 provider（沿用当前项目）重建 Agent，成功后保存 default_provider。
    Q_INVOKABLE bool initialize(const QString& providerName);

    // ── Provider 配置 ──
    Q_INVOKABLE QStringList listProviders() const;
    Q_INVOKABLE QVariantMap providerInfo(const QString& name) const;
    // 保存 provider 配置；values 含 "rename_to" 时执行改名（目标已存在则失败）。
    Q_INVOKABLE bool saveProvider(const QString& name, const QVariantMap& values);
    Q_INVOKABLE QString defaultProvider() const;
    Q_INVOKABLE bool hasUsableApiKey(const QString& name) const;
    // 新增一个空模板 provider 条目（自动生成唯一名"未命名"/"未命名-2"...），返回新名。
    Q_INVOKABLE QString addProvider();
    // 删除 provider；默认 provider 或当前运行中的 provider 拒绝删除。
    Q_INVOKABLE bool deleteProvider(const QString& name);

    // ── 项目操作 ──
    // 校验目录状态："valid"（可打开）/ "new"（空目录可新建）/ "occupied"（非空且无项目）。
    Q_INVOKABLE QString validateProjectDir(const QString& path) const;
    Q_INVOKABLE bool openProject(const QString& path);
    Q_INVOKABLE bool createProject(const QString& dirPath, const QString& title);
    // 最近项目列表，按最近优先：[{title, path, isCurrent}]；
    // title 来自项目 novel.json，读取失败回退显示路径本身。
    Q_INVOKABLE QVariantList recentProjects() const;

    // 从最近项目列表移除一条记录；命中返回 true（不影响已打开的项目）。
    Q_INVOKABLE bool removeRecentProject(const QString& path);
    Q_INVOKABLE QString lastProjectPath() const;

    // ── 固定目录项目管理（对齐原型）──
    // 固定项目根目录（~/.novelagent/projects），仅返回路径、不创建。
    Q_INVOKABLE QString projectsDir() const;
    // 在固定目录下创建项目并进入。返回状态码：
    // "ok" | "invalid_title" | "invalid_chars" | "duplicate" | "failed"。
    Q_INVOKABLE QString createProjectAt(const QString& title, const QString& description);
    // 全部项目列表（固定目录枚举 − 软删目录）：
    // [{title, path, isCurrent}]，按最近使用排序（不在 recent 的排最后）。
    Q_INVOKABLE QVariantList allProjects() const;
    // 软删项目：目录重命名为"原名（已删除）"并从 recent 移除；
    // 删除的是当前项目时 Agent 重建为无项目状态。磁盘内容保留。
    Q_INVOKABLE bool deleteProject(const QString& path);

    // ── 技能管理 ──
    // 技能列表：[{name, description, always, enabled}, ...]；未初始化返回空。
    Q_INVOKABLE QVariantList skillList() const;
    // 启用/禁用技能并持久化；生成中或技能不存在时返回 false。
    Q_INVOKABLE bool setSkillEnabled(const QString& name, bool enabled);

    // ── 调试 ──
    Q_INVOKABLE void setVerbose(bool enabled);
    Q_INVOKABLE bool verboseEnabled() const { return config_.verbose; }

signals:
    // 流式输出（逐 token 推送到 QML）
    void tokenReceived(const QString& sessionId, const QString& delta);
    void reasoningReceived(const QString& sessionId, const QString& delta);
    void toolCallStarted(const QString& sessionId, const QString& toolName);
    void toolCallFinished(const QString& sessionId, const QString& toolName, bool ok);
    void responseComplete(const QString& sessionId, const QString& fullText);
    void errorOccurred(const QString& message);
    // 新会话已创建或已切换会话，QML 据此重建聊天流
    void sessionReset();
    // 会话列表变化（新建/切换/删除/标题自动提取后），侧栏据此刷新
    void sessionsChanged();
    // 当前查看会话变化（多会话焦点）
    void currentSessionIdChanged();

    // 状态变化
    void agentReadyChanged();
    void providersChanged();
    void projectChanged();
    // 技能列表或启用状态变化（重建/开关切换/新技能保存后发射）
    void skillsChanged();
    // 章节数据可能变化（响应完成 / 手动刷新项目后发射）
    void chaptersChanged();
    void statusChanged(const QString& text);
    void busyChanged();
    // 当前查看会话的 busy 状态变化（runAgent 开始/完成、切换会话时发射）
    void sessionBusyChanged();
    void modelChanged();
    void usageChanged();

private:
    void setStatus(const QString& text);
    void runAgent(const std::string& session_id, std::string input);
    // 在当前线程同步执行一次索引（调用方负责线程/busy 约束），异常已内部吐掉。
    void runIndexUpdate(bool force);
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

    // 生命周期令牌（方案 B）：runAgent 的 on_complete 捕获其 weak_ptr，~QmlBridge 复位后
    // lock() 失败，兜底超时残留任务不访问已析构的本对象。
    std::shared_ptr<std::atomic<bool>> alive_;

    QString status_text_;
    // 索引进行中标志（自动索引与手动重建共用，供 busy() 聚合）。
    // shared_ptr 保活：池线程可在本对象析构后安全写/清标志，不触碰悬垂成员。
    std::shared_ptr<std::atomic<bool>> indexing_;
    std::atomic<bool> cancel_requested_{false};
    std::thread worker_;
    QString current_session_id_;  // 多会话并行：当前查看会话 id（阶段 4）
    QStringList recent_sessions_;  // 多会话并行：最近使用顺序（前端维护，B2/D3）
};

} // namespace qtui
