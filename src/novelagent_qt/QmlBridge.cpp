// QmlBridge 实现 — 串行模式下 Agent 与 QML 的线程安全桥接。

#include "novelagent_qt/QmlBridge.h"

#include "project/Models/Project.h"

#include <QMetaObject>

#include <spdlog/spdlog.h>

namespace qtui {

QmlBridge::QmlBridge(agent::Agent& agent, std::shared_ptr<Project> project,
                       QObject* parent)
    : QObject(parent)
    , agent_(agent)
    , project_(std::move(project))
{
    status_text_ = "就绪";
}

QmlBridge::~QmlBridge() {
    cancel_requested_.store(true);
    if (worker_.joinable())
        worker_.join();
}

// ── 属性读取 ──

QString QmlBridge::projectName() const {
    if (project_ && !project_->title.empty())
        return QString::fromStdString(project_->title);
    return QStringLiteral("未打开项目");
}

QString QmlBridge::modelName() const {
    return QString::fromStdString(agent_.client().config().model);
}

QString QmlBridge::providerName() const {
    return QString::fromStdString(agent_.client().config().name);
}

int QmlBridge::totalTokens() const {
    return 0;
}

int QmlBridge::contextPercent() const {
    return 0;
}

// ── QML 槽 ──

void QmlBridge::sendMessage(const QString& text) {
    if (text.trimmed().isEmpty()) return;
    if (busy_.load()) {
        spdlog::warn("[QmlBridge] 忽略重复请求（Agent 正忙）");
        return;
    }
    runAgent(text.toStdString());
}

void QmlBridge::cancelRequest() {
    if (busy_.load()) {
        agent_.requestCancel();
        setStatus(QStringLiteral("正在取消..."));
    }
}

void QmlBridge::newSession() {
    if (busy_.load()) return;
    agent_.resetSession();
    emit usageChanged();
    setStatus(QStringLiteral("新会话已创建"));
}

void QmlBridge::refreshProject() {
    emit projectChanged();
}

// ── 内部 ──

void QmlBridge::setStatus(const QString& text) {
    status_text_ = text;
    emit statusChanged(text);
}

void QmlBridge::runAgent(std::string input) {
    busy_.store(true);
    cancel_requested_.store(false);
    agent_.resetCancel();
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
                emit toolCallStarted(QString());
            }, Qt::QueuedConnection);
        };

        cb.on_error = [this](const std::string& error) {
            QString e = QString::fromStdString(error);
            QMetaObject::invokeMethod(this, [this, e]() {
                emit errorOccurred(e);
            }, Qt::QueuedConnection);
        };

        try {
            auto response = agent_.process(input, std::move(cb));

            QString fullText = QString::fromStdString(response.content);
            QString finishReason = QString::fromStdString(response.finish_reason);

            QMetaObject::invokeMethod(this, [this, fullText, finishReason]() {
                emit responseComplete(fullText);
                emit usageChanged();
                if (finishReason == "cancelled")
                    setStatus(QStringLiteral("已取消"));
                else
                    setStatus(QStringLiteral("就绪"));
            }, Qt::QueuedConnection);

        } catch (const std::exception& e) {
            QString err = QString::fromUtf8(e.what());
            QMetaObject::invokeMethod(this, [this, err]() {
                emit errorOccurred(err);
                setStatus(QStringLiteral("错误"));
            }, Qt::QueuedConnection);
        }

        QMetaObject::invokeMethod(this, [this]() {
            busy_.store(false);
            emit busyChanged();
        }, Qt::QueuedConnection);
    });

    worker_.detach();
}

} // namespace qtui
