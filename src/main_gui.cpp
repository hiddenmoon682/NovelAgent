// novelagent_gui — QML GUI 入口。
// 依赖 Qt6 Quick / QuickControls2。
#include "Bootstrap.h"
#include "novelagent_qt/QmlApp.h"

int main(int argc, char** argv) {
    auto ctx = bootstrap::run(argc, argv);
    if (ctx.exitCode >= 0) return ctx.exitCode;

    if (!ctx.execCommand.empty()) {
        ctx.app->runExec(ctx.execCommand);
        return 0;
    }

    if (ctx.cliMode) {
        ctx.app->runRepl();
        return 0;
    }

    return qtui::runQmlApp(argc, argv, *ctx.app);
}
