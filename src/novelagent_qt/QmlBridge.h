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
    Q_INVOKABLE bool saveProvider(const QString& name, const QVariantMap& values);
    Q_INVOKABLE QString defaultProvider() const;
    Q_INVOKABLE bool hasUsableApiKey(const QString& name) const;

    // ── 项目操作 ──
    // 校验目录状态："valid"（可打开）/ "new"（空目录可新建）/ "occupied"（非空且无项目）。
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
    // 章节数据可能变化（响应完成 / 手动刷新项目后发射）
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
