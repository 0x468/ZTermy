#include "platform/windows/DetachedWindowNativeFrame.h"

#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace ztermy::windowing
{
bool isUnobscuredDropTarget(QWindow &window, const QPointF localPosition, QWindow *movingWindow)
{
    if (!window.isVisible() || window.windowState() == Qt::WindowMinimized || &window == movingWindow
        || !std::isfinite(localPosition.x()) || !std::isfinite(localPosition.y()) || localPosition.x() < 0
        || localPosition.y() < 0 || localPosition.x() >= window.width() || localPosition.y() >= window.height())
        return false;
    const auto handle = reinterpret_cast<HWND>(window.winId()); // NOLINT(performance-no-int-to-ptr)
    const auto ignored = movingWindow
                             ? reinterpret_cast<HWND>(movingWindow->winId()) // NOLINT(performance-no-int-to-ptr)
                             : nullptr;
    POINT point{.x = qRound(localPosition.x() * window.devicePixelRatio()),
                .y = qRound(localPosition.y() * window.devicePixelRatio())};
    if (!ClientToScreen(handle, &point))
        return false;
    struct Probe
    {
        HWND target;
        HWND ignored;
        POINT point;
        bool found = false;
    } probe{.target = handle, .ignored = ignored, .point = point};
    EnumWindows(
        [](HWND candidate, LPARAM context) -> BOOL {
            auto &probe = *reinterpret_cast<Probe *>(context); // NOLINT(performance-no-int-to-ptr)
            if (candidate == probe.ignored || !IsWindowVisible(candidate) || IsIconic(candidate))
                return TRUE;
            DWORD cloaked = 0;
            if (SUCCEEDED(DwmGetWindowAttribute(candidate, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
                return TRUE;
            RECT bounds{};
            if (FAILED(DwmGetWindowAttribute(candidate, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds)))
                && !GetWindowRect(candidate, &bounds))
                return TRUE;
            if (!PtInRect(&bounds, probe.point))
                return TRUE;
            probe.found = candidate == probe.target;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&probe)); // NOLINT(performance-no-int-to-ptr)
    return probe.found;
}

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
    const auto interactive = window.property("windowChromeInteractive");
    const bool captionVisible =
        interactive.isValid() ? interactive.toBool() : window.property("windowControlsVisible").toBool();
    if (!captionVisible)
    {
        window.setProperty("nativeMaximizeButtonHovered", false);
        window.setProperty("nativeMaximizeButtonPressed", false);
    }
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
        const qreal stripHeight = window.property("titleTriggerHeight").toReal();
        const qreal buttonHeight =
            window.property("windowChromeHeight").isValid() ? window.property("windowChromeHeight").toReal() : 32.0;
        const qreal scale = window.devicePixelRatio();
        const int width = bounds.right - bounds.left;
        const HitTestMetrics metrics{
            .resizeBorder = resizeBorderForWindow(message.hwnd),
            .caption =
                captionVisible
                    ? Rect{.x = 0, .y = 0, .width = width - qRound(128 * scale), .height = qRound(buttonHeight * scale)}
                    : Rect{},
            .maximizeButton = {.x = width - qRound(buttonWidth * 2 * scale),
                               .y = 0,
                               .width = captionVisible ? qRound(buttonWidth * scale) : 0,
                               .height = captionVisible ? qRound(buttonHeight * scale) : 0},
            .topResizeBorder = stripHeight > 0 ? qRound(2 * scale) : -1,
            .dragStripHeight = captionVisible ? 0 : qRound(stripHeight * scale)};
        *result = toNativeHitArea(classifyHitTest(
            {.x = GET_X_LPARAM(message.lParam) - bounds.left, .y = GET_Y_LPARAM(message.lParam) - bounds.top},
            {.width = width, .height = bounds.bottom - bounds.top}, metrics, IsZoomed(message.hwnd) != FALSE));
        return true;
    }
    if (message.message == WM_NCMOUSEMOVE)
    {
        const bool hovered = captionVisible && message.wParam == HTMAXBUTTON;
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
        window.setProperty("nativeMaximizeButtonPressed", captionVisible);
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
