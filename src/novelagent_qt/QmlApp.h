#pragma once

// QmlApp — QML 应用入口。
//
// 创建 QGuiApplication + QQmlApplicationEngine，注册 QmlBridge 到 QML context，
// 加载 MainWindow.qml。由 main_gui.cpp 调用。

#include <memory>

struct Project;
struct ProviderConfig;
class NovelAgentApp;

namespace qtui {

// 启动 QML GUI 应用（阻塞直到窗口关闭）。
// argc/argv   命令行参数（QGuiApplication 需要）
// novelAgent  已完成装配的应用实例（由 main.cpp 创建）
// 返回进程退出码。
int runQmlApp(int argc, char** argv, NovelAgentApp& novelAgent);

} // namespace qtui
