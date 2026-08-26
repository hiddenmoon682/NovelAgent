// QmlApp 实现 — QML 应用入口。

#include "novelagent_qt/QmlApp.h"
#include "novelagent_qt/QmlBridge.h"
#ifdef Q_OS_WIN
#include "novelagent_qt/WinFrameBehavior.h"
#include <QWindow>
#endif

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QPalette>

#include <spdlog/spdlog.h>

namespace qtui {

int runQmlApp(int argc, char** argv) {
    // Qt Quick Controls 2 样式。用 Fusion 而非 Material：Fusion 的自定义 background/圆角/padding
    // 完全可预测，不会像 Material 那样动态推 topPadding（负值致占位符/光标飞出框外）或用偏浅默认遮罩。
    qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");

    QGuiApplication app(argc, argv);

    // Fusion 无 Material.theme:Dark，改用应用级 QPalette 表达深色主题与高亮色：
    // 让未被 Themed* 覆盖的默认渲染（滚动条/选中高亮/占位色/提示底色等）贴合 Theme 暖墨配色，
    // 高亮取 Theme.accent（朱砂）。色值保持与 Theme.qml 一致。
    {
        QPalette pal;
        pal.setColor(QPalette::Window,          QColor("#3d362b"));  // bgElevated
        pal.setColor(QPalette::WindowText,      QColor("#f0eadd"));  // textPrimary
        pal.setColor(QPalette::Base,            QColor("#2b251d"));  // bgField
        pal.setColor(QPalette::AlternateBase,   QColor("#2a251e"));  // bgReader
        pal.setColor(QPalette::Text,            QColor("#f0eadd"));  // textPrimary
        pal.setColor(QPalette::PlaceholderText, QColor("#7f7565"));  // textFaint
        pal.setColor(QPalette::Button,          QColor("#3d362b"));  // bgElevated
        pal.setColor(QPalette::ButtonText,      QColor("#f0eadd"));  // textPrimary
        pal.setColor(QPalette::Highlight,       QColor("#c9553e"));  // accent 朱砂
        pal.setColor(QPalette::HighlightedText, QColor("#f5efe2"));
        pal.setColor(QPalette::ToolTipBase,     QColor("#3d362b"));  // bgElevated
        pal.setColor(QPalette::ToolTipText,     QColor("#f0eadd"));  // textPrimary
        pal.setColor(QPalette::Disabled, QPalette::Text,       QColor("#7f7565"));
        pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#7f7565"));
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#7f7565"));
        app.setPalette(pal);
    }
#ifdef Q_OS_WIN
    // 无边框窗口原生行为补全：WM_NCHITTEST 补回边缘缩放命中区，
    // WM_SYSCOMMAND 补回任务栏点击最小化切换
    static WinFrameBehaviorFilter frameBehaviorFilter;
    app.installNativeEventFilter(&frameBehaviorFilter);
#endif
    app.setApplicationName(QStringLiteral("NovelAgent"));
    // QML Settings（窗口位置持久化）依赖 organizationName，缺省会初始化失败
    app.setOrganizationName(QStringLiteral("NovelAgent"));
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

    // QML 引擎错误（加载/运行时）输出到日志，便于定位
    QObject::connect(&engine, &QQmlEngine::warnings, [](const QList<QQmlError>& warnings) {
        for (const auto& w : warnings)
            spdlog::error("[QmlApp] QML warning: {}", w.toString().toStdString());
    });

    engine.load(url);
    if (engine.rootObjects().isEmpty()) {
        spdlog::error("[QmlApp] 无法加载 MainWindow.qml");
        return -1;
    }

#ifdef Q_OS_WIN
    // 无边框窗口样式补全：Qt 把 FramelessWindowHint 窗口创建为纯弹出式窗口，
    // Shell 不将其识别为正常窗口（任务栏点击无法最小化等），补回标准窗口样式；
    // 配合过滤器拦截 WM_NCCALCSIZE 零化非客户区，视觉上保持无边框
    qtui::fixupFramelessWindowStyle(
        qobject_cast<QWindow *>(engine.rootObjects().constFirst()));
#endif

    return app.exec();
}

} // namespace qtui
