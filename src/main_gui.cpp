// novelagent_gui — QML GUI 入口。
// 依赖 Qt6 Quick / QuickControls2。
#include "Bootstrap.h"
#include "novelagent_qt/QmlApp.h"

int main(int argc, char** argv) {
    auto ctx = bootstrap::run(argc, argv);
    if (ctx.exitCode >= 0) return ctx.exitCode;

    return qtui::runQmlApp(argc, argv, *ctx.app);
}
