#pragma once

// ============================================================================
// Bootstrap — 瘦身后只保留 SIGINT（Ctrl+C）优雅退出支持。
//
// 配置加载、Provider 校验、项目打开/创建、NovelAgentApp 构造
// 已全部迁移到 qtui::QmlBridge（GUI 内完成，见 QmlBridge::rebuildApp）。
// CLI 参数解析随 CLI11 依赖一并移除。
// ============================================================================

#include <atomic>
#include <csignal>

namespace bootstrap {

// 指向当前 Agent 的取消标志。QmlBridge 每次重建 NovelAgentApp 后更新；
// 销毁旧实例前先置空，保证信号处理函数不会访问悬垂指针。
inline std::atomic<std::atomic<bool>*> g_cancel_flag{nullptr};

// SIGINT 处理：只做原子读写，通知 Agent 主循环自行清理退出。
extern "C" inline void sigint_handler(int) {
    if (auto* flag = g_cancel_flag.load()) flag->store(true);
}

inline void installSigint() {
    signal(SIGINT, sigint_handler);
}

} // namespace bootstrap
