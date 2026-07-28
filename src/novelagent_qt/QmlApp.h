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
