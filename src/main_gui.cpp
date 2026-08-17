// novelagent_gui — QML GUI 入口。
// 依赖 Qt6 Quick / QuickControls2。
// 所有启动配置（provider / 项目 / 日志级别）由 QmlBridge 在 GUI 内完成。
#include "novelagent_qt/QmlApp.h"

int main(int argc, char** argv) {
    return qtui::runQmlApp(argc, argv);
}