#pragma once

// WinFrameBehavior — 无边框窗口的原生行为补全（Windows 原生事件过滤器）。
// Qt 对 FramelessWindowHint 的实现是创建纯弹出式窗口（WS_POPUP，不带
// WS_CAPTION/WS_SYSMENU/WS_MINIMIZEBOX 等样式），这导致窗口丢失多项由
// 系统基于窗口样式提供的交互能力（任务栏点击最小化、Alt+Space 系统菜单、
// 边缘缩放等——Shell 依据样式位决定是否向窗口投递这些交互）。
//
// 修复采用业界标准做法（Electron/VSCode 同款）：
//   1) fixupFramelessWindowStyle() 补回标准窗口样式（WS_CAPTION |
//      WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX），
//      使 Shell 将窗口识别为正常窗口；
//   2) 拦截 WM_NCCALCSIZE 零化非客户区，客户区占满整个窗口，
//      视觉上仍保持无边框，不绘制系统标题栏与边框；
//   3) 拦截 WM_NCHITTEST，在窗口边缘命中区返回对应 HT* 值，
//      由系统接管缩放光标与拖拽缩放。
// 样式补全后任务栏点击最小化/恢复、Alt+Space 系统菜单等均由
// DefWindowProc 原生处理，无需拦截 WM_SYSCOMMAND（拦截反而会因 Qt 侧
// visibility 与原生窗口状态不同步造成状态混乱）。

#include <QAbstractNativeEventFilter>

class QWindow;

namespace qtui {

class WinFrameBehaviorFilter : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray &eventType, void *message,
                           qintptr *result) override;
};

// 为无边框窗口补回标准窗口样式（详见头注释）；非 Windows 平台为空实现。
// 必须在窗口已创建原生句柄后调用（内部经 winId() 保证）。
void fixupFramelessWindowStyle(QWindow *window);

} // namespace qtui
