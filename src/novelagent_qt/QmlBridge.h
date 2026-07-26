#pragma once

// QmlBridge — C++ ↔ QML 单一桥接对象。
//
// 持有 Agent& 与 Project&，通过 Q_INVOKABLE 槽接收 QML 请求，
// 通过信号将 Agent 的流式输出跨线程推送回 QML 主线程。
//
// 线程模型（串行模式）：
//   - sendMessage() 在独立工作线程中调用 Agent::process()，避免阻塞 UI
//   - StreamCallbacks 在 HTTP 线程触发，经 QMetaObject::invokeMethod
//     (Qt::QueuedConnection) 转发到 QML 主线程
//   - busy_ 原子标志保证同一时刻只有一个请求在执行（串行）

#include "agent/core/Agent.h"
#include "llm/ILLMClient.h"

#include <QObject>
#include <QString>
#include <QVariantList>

#include <atomic>
#include <memory>
#include <thread>

struct Project;

namespace qtui {

class QmlBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString modelName READ modelName NOTIFY modelChanged)
    Q_PROPERTY(QString providerName READ providerName NOTIFY modelChanged)
    Q_PROPERTY(int totalTokens READ totalTokens NOTIFY usageChanged)
    Q_PROPERTY(int contextPercent READ contextPercent NOTIFY usageChanged)

public:
    explicit QmlBridge(agent::Agent& agent, std::shared_ptr<Project> project,
                       QObject* parent = nullptr);
    ~QmlBridge() override;

    // ── 属性读取 ──
    QString projectName() const;
    QString statusText() const { return status_text_; }
    bool busy() const { return busy_.load(); }
    QString modelName() const;
    QString providerName() const;
    int totalTokens() const;
    int contextPercent() const;

    // ── QML 可调用的槽 ──
    Q_INVOKABLE void sendMessage(const QString& text);
    Q_INVOKABLE void cancelRequest();
    Q_INVOKABLE void newSession();
    Q_INVOKABLE void refreshProject();
    // 章节列表（按 order 升序）：[{id, title, order, wordCount}, ...]；项目未打开返回空。
    Q_INVOKABLE QVariantList chapterList() const;
    // 读取章节正文 Markdown；失败返回空串并 emit errorOccurred。
    Q_INVOKABLE QString loadChapter(const QString& chapterId);

signals:
    // 流式输出（逐 token 推送到 QML）
    void tokenReceived(const QString& delta);
    void reasoningReceived(const QString& delta);
    void toolCallStarted(const QString& toolName);
    void toolCallFinished(const QString& toolName, bool ok);
    void responseComplete(const QString& fullText);
    void errorOccurred(const QString& message);

    // 状态变化
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

    agent::Agent& agent_;
    std::shared_ptr<Project> project_;

    QString status_text_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancel_requested_{false};
    std::thread worker_;
};

} // namespace qtui
