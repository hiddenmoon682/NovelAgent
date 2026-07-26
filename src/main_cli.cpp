// novelagent_cli — 终端 REPL / 单次命令入口。
// 不依赖 Qt，可独立编译。
#include "Bootstrap.h"

int main(int argc, char** argv) {
    auto ctx = bootstrap::run(argc, argv);
    if (ctx.exitCode >= 0) return ctx.exitCode;

    if (!ctx.execCommand.empty()) {
        ctx.app->runExec(ctx.execCommand);
        return 0;
    }

    ctx.app->runRepl();
    return 0;
}
