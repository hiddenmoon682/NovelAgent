// QmlApp 实现 — QML 应用入口。

#include "novelagent_qt/QmlApp.h"
#include "novelagent_qt/QmlBridge.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <spdlog/spdlog.h>

namespace qtui {

int runQmlApp(int argc, char** argv) {
    // Qt Quick Controls 2 样式（Material 风格接近设计稿深色主题）
    qputenv("QT_QUICK_CONTROLS_STYLE", "Material");

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("NovelAgent"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));

    QmlBridge bridge;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("bridge"), &bridge);

    const QUrl url(QStringLiteral("qrc:/qml/MainWindow.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject* obj, const QUrl& objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    engine.load(url);
    if (engine.rootObjects().isEmpty()) {
        spdlog::error("[QmlApp] 无法加载 MainWindow.qml");
        return -1;
    }

    return app.exec();
}

} // namespace qtui
