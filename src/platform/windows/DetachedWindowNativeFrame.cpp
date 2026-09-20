#include "platform/windows/DetachedWindowNativeFrame.h"

#include <windowsx.h>
#include <algorithm>

namespace ztermy::windowing
{
LRESULT toNativeHitArea(const HitArea area) noexcept
{
    using enum HitArea;
    switch (area)
    {
        case Caption:
            return HTCAPTION;
        case MaximizeButton:
            return HTMAXBUTTON;
        case Left:
            return HTLEFT;
        case Top:
            return HTTOP;
        case Right:
            return HTRIGHT;
        case Bottom:
            return HTBOTTOM;
        case TopLeft:
            return HTTOPLEFT;
        case TopRight:
            return HTTOPRIGHT;
        case BottomLeft:
            return HTBOTTOMLEFT;
        case BottomRight:
            return HTBOTTOMRIGHT;
        case Client:
        default:
            return HTCLIENT;
    }
}

int resizeBorderForWindow(const HWND windowHandle) noexcept
{
    static thread_local UINT cachedDpi = 0;
    static thread_local int cachedBorder = 1;
    const UINT dpi = GetDpiForWindow(windowHandle);
    if (dpi != cachedDpi)
    {
        cachedBorder =
            (std::max)(GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi), 1);
        cachedDpi = dpi;
    }
    return cachedBorder;
}

bool handleDetachedWindowFrameMessage(QQuickWindow &window, const MSG &message, qintptr *result)
{
    if (message.message == WM_NCCALCSIZE && message.wParam != FALSE)
    {
        if (result == nullptr)
            return false;
        if (IsZoomed(message.hwnd) != FALSE)
        {
            MONITORINFO monitor{.cbSize = sizeof(MONITORINFO)};
            if (GetMonitorInfoW(MonitorFromWindow(message.hwnd, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE)
            {
                auto *parameters =
                    reinterpret_cast<NCCALCSIZE_PARAMS *>(message.lParam); // NOLINT(performance-no-int-to-ptr)
                parameters->rgrc[0] = monitor.rcWork;
            }
        }
        *result = 0;
        return true;
    }
    if (message.message == WM_NCHITTEST)
    {
        if (result == nullptr)
            return false;
        RECT bounds{};
        if (GetWindowRect(message.hwnd, &bounds) == FALSE)
            return false;
        constexpr qreal buttonWidth = 32.0;
        constexpr qreal buttonHeight = 32.0;
        const qreal scale = window.devicePixelRatio();
        const int width = bounds.right - bounds.left;
        const HitTestMetrics metrics{.resizeBorder = resizeBorderForWindow(message.hwnd),
                                     .caption = {},
                                     .maximizeButton = {.x = width - qRound(buttonWidth * 2 * scale),
                                                        .y = 0,
                                                        .width = qRound(buttonWidth * scale),
                                                        .height = qRound(buttonHeight * scale)}};
        *result = toNativeHitArea(classifyHitTest(
            {.x = GET_X_LPARAM(message.lParam) - bounds.left, .y = GET_Y_LPARAM(message.lParam) - bounds.top},
            {.width = width, .height = bounds.bottom - bounds.top}, metrics, IsZoomed(message.hwnd) != FALSE));
        return true;
    }
    if (message.message == WM_NCMOUSEMOVE)
    {
        const bool hovered = message.wParam == HTMAXBUTTON;
        window.setProperty("nativeMaximizeButtonHovered", hovered);
        if (hovered)
        {
            TRACKMOUSEEVENT tracking{.cbSize = sizeof(TRACKMOUSEEVENT),
                                     .dwFlags = TME_LEAVE | TME_NONCLIENT,
                                     .hwndTrack = message.hwnd,
                                     .dwHoverTime = HOVER_DEFAULT};
            TrackMouseEvent(&tracking);
            const LRESULT nativeResult = DefWindowProcW(message.hwnd, message.message, message.wParam, message.lParam);
            if (result != nullptr)
                *result = nativeResult;
            return true;
        }
    }
    if (message.message == WM_NCMOUSELEAVE)
    {
        window.setProperty("nativeMaximizeButtonHovered", false);
        window.setProperty("nativeMaximizeButtonPressed", false);
    }
    if (message.message == WM_NCLBUTTONDOWN && message.wParam == HTMAXBUTTON)
    {
        window.setProperty("nativeMaximizeButtonPressed", true);
        if (result != nullptr)
            *result = 0;
        return true;
    }
    if (message.message == WM_NCLBUTTONUP && message.wParam == HTMAXBUTTON)
    {
        const bool wasPressed = window.property("nativeMaximizeButtonPressed").toBool();
        window.setProperty("nativeMaximizeButtonPressed", false);
        if (wasPressed)
            PostMessageW(message.hwnd, WM_SYSCOMMAND, IsZoomed(message.hwnd) != FALSE ? SC_RESTORE : SC_MAXIMIZE, 0);
        if (result != nullptr)
            *result = 0;
        return true;
    }
    return false;
}
} // namespace ztermy::windowing
