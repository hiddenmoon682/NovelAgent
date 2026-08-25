// WinFrameBehavior 实现 — 仅 Windows 生效，其他平台编译为空实现。

#include "novelagent_qt/WinFrameBehavior.h"

#ifdef Q_OS_WIN

#include <QGuiApplication>
#include <QWindow>

#include <windows.h>
#include <windowsx.h>

namespace qtui {

namespace {
// 边缘命中区宽度（px）：过窄难命中，过宽会侵蚀标题栏拖拽区
constexpr int kBorderMargin = 8;

// 按 HWND 反查本进程的顶层 QWindow；非本应用窗口返回 nullptr，
// 避免干扰其他进程/控件的消息处理
QWindow *findTopLevelWindow(HWND hwnd) {
    for (QWindow *w : QGuiApplication::topLevelWindows()) {
        if (w->winId() == reinterpret_cast<WId>(hwnd))
            return w;
    }
    return nullptr;
}

// 非客户区零化：样式上窗口已带 WS_CAPTION/WS_THICKFRAME（Shell 据此
// 识别为正常窗口），但返回 0 使客户区占满整个窗口，系统不再绘制
// 标题栏与边框，视觉上保持无边框
bool handleNcCalcSize(MSG *msg, qintptr *result) {
    // wParam 为 FALSE 时仅需返回默认值，不干预
    if (!msg->wParam)
        return false;
    auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
    if (IsZoomed(msg->hwnd)) {
        // 最大化时窗口矩形会外溢不可见边框宽度，直接零化会裁切顶部内容；
        // 改用显示器工作区作为客户区（自然避开任务栏）
        MONITORINFO mi{};
        mi.cbSize = sizeof(MONITORINFO);
        const HMONITOR mon = MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST);
        if (mon && GetMonitorInfoW(mon, &mi))
            params->rgrc[0] = mi.rcWork;
    }
    *result = 0;
    return true;
}

// 边缘拖拽缩放：窗口边缘 8px 命中区返回 HT*，系统据此接管缩放光标与拖拽
bool handleNcHitTest(MSG *msg, QWindow *win, qintptr *result) {
    // 最大化/全屏时贴边无缩放语义
    if (win->visibility() == QWindow::Maximized ||
        win->visibility() == QWindow::FullScreen)
        return false;

    RECT rc;
    if (!GetWindowRect(msg->hwnd, &rc))
        return false;

    const int x = GET_X_LPARAM(msg->lParam);
    const int y = GET_Y_LPARAM(msg->lParam);
    const bool left = x < rc.left + kBorderMargin;
    const bool right = x >= rc.right - kBorderMargin;
    const bool top = y < rc.top + kBorderMargin;
    const bool bottom = y >= rc.bottom - kBorderMargin;

    // 角优先于边，避免角部只能单向缩放；命中值经 Qt result 出参返回
    if (top && left) { *result = HTTOPLEFT; return true; }
    if (top && right) { *result = HTTOPRIGHT; return true; }
    if (bottom && left) { *result = HTBOTTOMLEFT; return true; }
    if (bottom && right) { *result = HTBOTTOMRIGHT; return true; }
    if (left) { *result = HTLEFT; return true; }
    if (right) { *result = HTRIGHT; return true; }
    if (top) { *result = HTTOP; return true; }
    if (bottom) { *result = HTBOTTOM; return true; }
    return false;
}

} // namespace

bool WinFrameBehaviorFilter::nativeEventFilter(const QByteArray &eventType,
                                               void *message, qintptr *result) {
    if (eventType != "windows_generic_MSG")
        return false;
    auto *msg = static_cast<MSG *>(message);
    if (msg->message != WM_NCHITTEST && msg->message != WM_NCCALCSIZE)
        return false;

    // WM_NCCALCSIZE 对所有顶层窗口生效（含弹窗），无需反查 QWindow
    if (msg->message == WM_NCCALCSIZE)
        return handleNcCalcSize(msg, result);

    QWindow *win = findTopLevelWindow(msg->hwnd);
    if (!win)
        return false;

    return handleNcHitTest(msg, win, result);
}

void fixupFramelessWindowStyle(QWindow *window) {
    if (!window)
        return;
    // winId() 确保原生窗口已创建；根窗口在 engine.load() 后必然已创建
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    // 纯弹出式 → 带标题栏语义的标准可调窗口：Shell 依据这些样式位
    // 决定是否投递任务栏点击最小化等交互（WS_CHILD 与 WS_POPUP 互斥，一并清除）
    style &= ~(WS_POPUP | WS_CHILD);
    style |= WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    // 触发 WM_NCCALCSIZE 重算非客户区，使新样式立即生效
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

} // namespace qtui

#else // !Q_OS_WIN

namespace qtui {
// 非 Windows 平台空实现，保证跨平台链接不缺失符号
void fixupFramelessWindowStyle(QWindow *) {}
} // namespace qtui

#endif // Q_OS_WIN
