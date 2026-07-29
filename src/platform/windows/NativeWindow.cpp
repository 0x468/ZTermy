#include "platform/windows/NativeWindow.h"

#include "platform/windows/WindowHitTest.h"

#include <QEvent>
#include <QLoggingCategory>
#include <QQmlContext>
#include <QScreen>

#include <Windows.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>

namespace
{

Q_LOGGING_CATEGORY(windowLog, "ztermy.window")

constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr DWORD kDwmWindowCornerPreference = 33;
constexpr DWORD kDwmSystemBackdropType = 38;
constexpr int kDwmWindowCornerRound = 2;
constexpr int kDwmSystemBackdropMainWindow = 2;
constexpr auto kNativeWindowProperty = L"ztermy.NativeWindow";
constexpr UINT kNcUahDrawCaption = 0x00AE;
constexpr UINT kNcUahDrawFrame = 0x00AF;

[[nodiscard]] LRESULT toNativeHitArea(const ztermy::windowing::HitArea area) noexcept
{
    using enum ztermy::windowing::HitArea;

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

} // namespace

namespace ztermy
{

NativeWindow::NativeWindow(QWindow *parent) : QQuickView(parent)
{
    setFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint
             | Qt::WindowCloseButtonHint);
    setTitle(QStringLiteral("ztermy"));
    setResizeMode(QQuickView::SizeRootObjectToView);
    setMinimumSize(QSize(500, 360));
    resize(1180, 760);
    setColor(Qt::transparent);
}

NativeWindow::~NativeWindow()
{
    uninstallWindowProcedure();
}

bool NativeWindow::load()
{
    rootContext()->setContextProperty(QStringLiteral("windowChrome"), this);
    setSource(QUrl(QStringLiteral("qrc:/qt/qml/Ztermy/Main.qml")));

    if (status() == QQuickView::Error)
    {
        return false;
    }

    configureNativeWindow();
    return true;
}

bool NativeWindow::maximized() const noexcept
{
    return windowStates().testFlag(Qt::WindowMaximized);
}

bool NativeWindow::maximizeButtonHovered() const noexcept
{
    return m_maximizeButtonHovered;
}

bool NativeWindow::maximizeButtonPressed() const noexcept
{
    return m_maximizeButtonPressed;
}

void NativeWindow::minimizeWindow()
{
    showMinimized();
}

void NativeWindow::toggleMaximize()
{
    maximized() ? showNormal() : showMaximized();
}

void NativeWindow::closeWindow()
{
    close();
}

void NativeWindow::setTitleBarMetrics(const qreal titleHeight, const qreal controlsLeft, const qreal maximizeLeft,
                                      const qreal maximizeWidth)
{
    m_titleHeight = titleHeight;
    m_controlsLeft = controlsLeft;
    m_maximizeLeft = maximizeLeft;
    m_maximizeWidth = maximizeWidth;
    qCDebug(windowLog) << "title bar metrics"
                       << "height=" << m_titleHeight << "controlsLeft=" << m_controlsLeft
                       << "maximizeLeft=" << m_maximizeLeft << "maximizeWidth=" << m_maximizeWidth
                       << "dpr=" << devicePixelRatio();
}

bool NativeWindow::event(QEvent *event)
{
    const bool handled = QQuickView::event(event);

    if (event->type() == QEvent::WindowStateChange)
    {
        emit maximizedChanged();
    }

    return handled;
}

bool NativeWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    const auto nativeMessage = static_cast<MSG *>(message);
    const HWND windowHandle = nativeMessage->hwnd;

    switch (nativeMessage->message)
    {
        case WM_NCCALCSIZE:
            if (nativeMessage->wParam != FALSE)
            {
                auto *parameters =
                    reinterpret_cast<NCCALCSIZE_PARAMS *>(nativeMessage->lParam); // NOLINT(performance-no-int-to-ptr)
                if (parameters != nullptr && IsZoomed(windowHandle) != FALSE)
                {
                    const HMONITOR monitor = MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST);
                    MONITORINFO monitorInfo{.cbSize = sizeof(MONITORINFO)};
                    if (GetMonitorInfoW(monitor, &monitorInfo) != FALSE)
                    {
                        const RECT proposed = parameters->rgrc[0];
                        const RECT workArea = monitorInfo.rcWork;
                        const windowing::Rect constrained =
                            windowing::constrainMaximizedClientRect({.x = proposed.left,
                                                                     .y = proposed.top,
                                                                     .width = proposed.right - proposed.left,
                                                                     .height = proposed.bottom - proposed.top},
                                                                    {.x = workArea.left,
                                                                     .y = workArea.top,
                                                                     .width = workArea.right - workArea.left,
                                                                     .height = workArea.bottom - workArea.top});
                        parameters->rgrc[0] = {
                            .left = constrained.x,
                            .top = constrained.y,
                            .right = constrained.x + constrained.width,
                            .bottom = constrained.y + constrained.height,
                        };
                    }
                }
                *result = 0;
                return true;
            }
            break;

        case WM_NCHITTEST:
            *result = nativeHitTest(windowHandle, nativeMessage->lParam);
            return true;

        case WM_NCMOUSEMOVE:
        {
            const bool hoveringMaximize = nativeMessage->wParam == HTMAXBUTTON;
            setMaximizeButtonHovered(hoveringMaximize);

            if (hoveringMaximize)
            {
                TRACKMOUSEEVENT tracking{
                    .cbSize = sizeof(TRACKMOUSEEVENT),
                    .dwFlags = TME_LEAVE | TME_NONCLIENT,
                    .hwndTrack = windowHandle,
                    .dwHoverTime = HOVER_DEFAULT,
                };
                TrackMouseEvent(&tracking);
            }
            break;
        }

        case WM_NCMOUSELEAVE:
            setMaximizeButtonHovered(false);
            setMaximizeButtonPressed(false);
            qCDebug(windowLog) << "WM_NCMOUSELEAVE";
            break;

        case WM_NCLBUTTONDOWN:
            if (nativeMessage->wParam == HTMAXBUTTON)
            {
                setMaximizeButtonPressed(true);
                qCInfo(windowLog) << "WM_NCLBUTTONDOWN HTMAXBUTTON";
                *result = 0;
                return true;
            }
            break;

        case WM_NCLBUTTONUP:
            if (nativeMessage->wParam == HTMAXBUTTON)
            {
                const bool wasPressed = m_maximizeButtonPressed;
                setMaximizeButtonPressed(false);
                qCInfo(windowLog) << "WM_NCLBUTTONUP HTMAXBUTTON"
                                  << "pressed=" << wasPressed << "maximized=" << maximized();
                if (wasPressed)
                {
                    PostMessageW(windowHandle, WM_SYSCOMMAND, maximized() ? SC_RESTORE : SC_MAXIMIZE, 0);
                }
                *result = 0;
                return true;
            }
            setMaximizeButtonPressed(false);
            break;

        case WM_GETTITLEBARINFOEX:
        {
            auto *titleBarInfo =
                reinterpret_cast<TITLEBARINFOEX *>(nativeMessage->lParam); // NOLINT(performance-no-int-to-ptr)
            if (titleBarInfo == nullptr || titleBarInfo->cbSize < sizeof(TITLEBARINFOEX))
            {
                break;
            }

            RECT clientRect{};
            GetClientRect(windowHandle, &clientRect);
            const qreal scale = devicePixelRatio();
            const int titleHeight = qRound(m_titleHeight * scale);
            const int minimizeLeft = qRound(m_controlsLeft * scale);
            const int maximizeLeft = qRound(m_maximizeLeft * scale);
            const int buttonWidth = qRound(m_maximizeWidth * scale);

            POINT windowOrigin{.x = 0, .y = 0};
            ClientToScreen(windowHandle, &windowOrigin);

            const auto screenRect = [&windowOrigin](const int left, const int top, const int right, const int bottom) {
                return RECT{
                    .left = windowOrigin.x + left,
                    .top = windowOrigin.y + top,
                    .right = windowOrigin.x + right,
                    .bottom = windowOrigin.y + bottom,
                };
            };

            titleBarInfo->rcTitleBar = screenRect(0, 0, clientRect.right - clientRect.left, titleHeight);
            titleBarInfo->rgstate[0] = STATE_SYSTEM_FOCUSABLE;
            titleBarInfo->rgstate[1] = STATE_SYSTEM_INVISIBLE;
            titleBarInfo->rgstate[2] = STATE_SYSTEM_FOCUSABLE;
            titleBarInfo->rgstate[3] = STATE_SYSTEM_FOCUSABLE;
            titleBarInfo->rgstate[4] = STATE_SYSTEM_INVISIBLE;
            titleBarInfo->rgstate[5] = STATE_SYSTEM_FOCUSABLE;
            titleBarInfo->rgrect[0] = titleBarInfo->rcTitleBar;
            titleBarInfo->rgrect[1] = {};
            titleBarInfo->rgrect[2] = screenRect(minimizeLeft, 0, maximizeLeft, titleHeight);
            titleBarInfo->rgrect[3] = screenRect(maximizeLeft, 0, maximizeLeft + buttonWidth, titleHeight);
            titleBarInfo->rgrect[4] = {};
            titleBarInfo->rgrect[5] =
                screenRect(maximizeLeft + buttonWidth, 0, maximizeLeft + (buttonWidth * 2), titleHeight);

            qCInfo(windowLog) << "WM_GETTITLEBARINFOEX"
                              << "titleRect=" << titleBarInfo->rcTitleBar.left << titleBarInfo->rcTitleBar.top
                              << titleBarInfo->rcTitleBar.right << titleBarInfo->rcTitleBar.bottom
                              << "maximizeRect=" << titleBarInfo->rgrect[3].left << titleBarInfo->rgrect[3].top
                              << titleBarInfo->rgrect[3].right << titleBarInfo->rgrect[3].bottom;
            *result = 0;
            return true;
        }

        case WM_GETMINMAXINFO:
        {
            const HMONITOR monitor = MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST);
            MONITORINFO monitorInfo{.cbSize = sizeof(MONITORINFO)};
            auto *minMaxInfo =
                reinterpret_cast<MINMAXINFO *>(nativeMessage->lParam); // NOLINT(performance-no-int-to-ptr)
            const UINT windowDpi =
                std::max<UINT>(GetDpiForWindow(windowHandle), static_cast<UINT>(USER_DEFAULT_SCREEN_DPI));
            const windowing::Size minimumTrackSize = windowing::scaleLogicalSizeForDpi(
                {.width = minimumSize().width(), .height = minimumSize().height()}, windowDpi);
            minMaxInfo->ptMinTrackSize.x =
                std::max<LONG>(minMaxInfo->ptMinTrackSize.x, static_cast<LONG>(minimumTrackSize.width));
            minMaxInfo->ptMinTrackSize.y =
                std::max<LONG>(minMaxInfo->ptMinTrackSize.y, static_cast<LONG>(minimumTrackSize.height));

            if (GetMonitorInfoW(monitor, &monitorInfo) != FALSE)
            {
                const RECT &workArea = monitorInfo.rcWork;
                const RECT &monitorArea = monitorInfo.rcMonitor;

                minMaxInfo->ptMaxPosition.x = workArea.left - monitorArea.left;
                minMaxInfo->ptMaxPosition.y = workArea.top - monitorArea.top;
                minMaxInfo->ptMaxSize.x = workArea.right - workArea.left;
                minMaxInfo->ptMaxSize.y = workArea.bottom - workArea.top;
                *result = 0;
                return true;
            }
            break;
        }

        default:
            break;
    }

    return QQuickView::nativeEvent(eventType, message, result);
}

LRESULT CALLBACK NativeWindow::windowProcedure(const HWND windowHandle, const UINT message, const WPARAM wParam,
                                               const LPARAM lParam)
{
    auto *window = static_cast<NativeWindow *>(GetPropW(windowHandle, kNativeWindowProperty));
    if (window == nullptr || window->m_originalWindowProcedure == nullptr)
    {
        return DefWindowProcW(windowHandle, message, wParam, lParam);
    }

    LRESULT result = 0;
    if (window->handleWindowProcedureMessage(windowHandle, message, wParam, lParam, &result))
    {
        return result;
    }
    return CallWindowProcW(window->m_originalWindowProcedure, windowHandle, message, wParam, lParam);
}

LRESULT NativeWindow::nativeHitTest(const HWND windowHandle, const LPARAM lParam) const
{
    POINT clientPoint{
        .x = GET_X_LPARAM(lParam),
        .y = GET_Y_LPARAM(lParam),
    };
    ScreenToClient(windowHandle, &clientPoint);

    RECT clientRect{};
    GetClientRect(windowHandle, &clientRect);

    const UINT dpi = GetDpiForWindow(windowHandle);
    const int frame = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi);
    const int padding = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    const int resizeBorder = std::max(frame + padding, 1);
    const qreal scale = devicePixelRatio();

    const windowing::HitTestMetrics metrics{
        .resizeBorder = resizeBorder,
        .caption =
            {
                .x = 0,
                .y = 0,
                .width = qRound(m_controlsLeft * scale),
                .height = qRound(m_titleHeight * scale),
            },
        .maximizeButton =
            {
                .x = qRound(m_maximizeLeft * scale),
                .y = 0,
                .width = qRound(m_maximizeWidth * scale),
                .height = qRound(m_titleHeight * scale),
            },
    };

    const auto area =
        windowing::classifyHitTest({.x = clientPoint.x, .y = clientPoint.y},
                                   {.width = clientRect.right, .height = clientRect.bottom}, metrics, maximized());

    if (area == windowing::HitArea::MaximizeButton)
    {
        qCDebug(windowLog) << "WM_NCHITTEST -> HTMAXBUTTON"
                           << "clientPoint=" << clientPoint.x << clientPoint.y
                           << "buttonRect=" << metrics.maximizeButton.x << metrics.maximizeButton.y
                           << metrics.maximizeButton.width << metrics.maximizeButton.height;
    }

    return toNativeHitArea(area);
}

bool NativeWindow::handleWindowProcedureMessage(const HWND windowHandle, const UINT message, const WPARAM wParam,
                                                const LPARAM lParam, LRESULT *result)
{
    switch (message)
    {
        case kNcUahDrawCaption:
        case kNcUahDrawFrame:
            // These undocumented theme messages can make DefWindowProc draw
            // legacy non-client chrome over a custom frame during transitions.
            *result = 0;
            return true;

        case WM_NCHITTEST:
            *result = nativeHitTest(windowHandle, lParam);
            return true;

        case WM_NCMOUSEMOVE:
            if (wParam == HTMAXBUTTON)
            {
                setMaximizeButtonHovered(true);
                *result = DefWindowProcW(windowHandle, message, wParam, lParam);
                return true;
            }
            setMaximizeButtonHovered(false);
            break;

        case WM_NCMOUSELEAVE:
            setMaximizeButtonHovered(false);
            setMaximizeButtonPressed(false);
            break;

        case WM_NCLBUTTONDOWN:
            if (wParam == HTMAXBUTTON)
            {
                setMaximizeButtonPressed(true);
                qCInfo(windowLog) << "hooked WM_NCLBUTTONDOWN HTMAXBUTTON";
                *result = 0;
                return true;
            }
            break;

        case WM_NCLBUTTONUP:
            if (wParam == HTMAXBUTTON)
            {
                const bool wasPressed = m_maximizeButtonPressed;
                setMaximizeButtonPressed(false);
                qCInfo(windowLog) << "hooked WM_NCLBUTTONUP HTMAXBUTTON"
                                  << "pressed=" << wasPressed << "maximized=" << maximized();
                if (wasPressed)
                {
                    PostMessageW(windowHandle, WM_SYSCOMMAND, maximized() ? SC_RESTORE : SC_MAXIMIZE, 0);
                }
                *result = 0;
                return true;
            }
            setMaximizeButtonPressed(false);
            break;

        default:
            break;
    }
    return false;
}

void NativeWindow::installWindowProcedure(const HWND windowHandle)
{
    if (m_windowHandle != nullptr)
    {
        return;
    }

    if (SetPropW(windowHandle, kNativeWindowProperty, this) == FALSE)
    {
        qCCritical(windowLog) << "failed to associate native window property"
                              << "error=" << GetLastError();
        return;
    }

    SetLastError(ERROR_SUCCESS);
    const auto procedure = reinterpret_cast<WNDPROC>( // NOLINT(performance-no-int-to-ptr)
        SetWindowLongPtrW(windowHandle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&NativeWindow::windowProcedure)));
    if (procedure == nullptr && GetLastError() != ERROR_SUCCESS)
    {
        qCCritical(windowLog) << "failed to install native window procedure"
                              << "error=" << GetLastError();
        RemovePropW(windowHandle, kNativeWindowProperty);
        return;
    }

    m_windowHandle = windowHandle;
    m_originalWindowProcedure = procedure;
    qCInfo(windowLog) << "installed native window procedure"
                      << "hwnd=" << windowHandle;
}

void NativeWindow::uninstallWindowProcedure()
{
    if (m_windowHandle == nullptr)
    {
        return;
    }

    if (m_originalWindowProcedure != nullptr && IsWindow(m_windowHandle) != FALSE)
    {
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        const auto currentProcedure = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(m_windowHandle, GWLP_WNDPROC));
        if (currentProcedure == reinterpret_cast<WNDPROC>(&NativeWindow::windowProcedure))
        {
            SetWindowLongPtrW(m_windowHandle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_originalWindowProcedure));
        }
        RemovePropW(m_windowHandle, kNativeWindowProperty);
    }

    m_windowHandle = nullptr;
    m_originalWindowProcedure = nullptr;
}

void NativeWindow::configureNativeWindow()
{
    const auto windowHandle = reinterpret_cast<HWND>(winId()); // NOLINT(performance-no-int-to-ptr)
    LONG_PTR style = GetWindowLongPtrW(windowHandle, GWL_STYLE);
    // Keep the DWM-recognized frame metadata so Windows can provide modern
    // maximize/restore transitions and Snap Layout. Removing WS_SYSMENU keeps
    // the native caption buttons from being painted over our custom chrome.
    style |= WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    style &= ~WS_SYSMENU;
    SetWindowLongPtrW(windowHandle, GWL_STYLE, style);
    qCInfo(windowLog) << "configure native window"
                      << "hwnd=" << windowHandle << "qtFlags=" << flags() << "style=" << Qt::hex << style;

    installWindowProcedure(windowHandle);
    SetWindowPos(windowHandle, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    applyBackdrop();
}

void NativeWindow::applyBackdrop()
{
    const auto windowHandle = reinterpret_cast<HWND>(winId()); // NOLINT(performance-no-int-to-ptr)
    const BOOL darkMode = TRUE;
    const int cornerPreference = kDwmWindowCornerRound;
    const int backdropType = kDwmSystemBackdropMainWindow;
    const MARGINS frameMargins{
        .cxLeftWidth = 1,
        .cxRightWidth = 1,
        .cyTopHeight = 1,
        .cyBottomHeight = 1,
    };

    DwmSetWindowAttribute(windowHandle, kDwmUseImmersiveDarkMode, &darkMode, sizeof(darkMode));
    DwmSetWindowAttribute(windowHandle, kDwmWindowCornerPreference, &cornerPreference, sizeof(cornerPreference));
    DwmSetWindowAttribute(windowHandle, kDwmSystemBackdropType, &backdropType, sizeof(backdropType));
    DwmExtendFrameIntoClientArea(windowHandle, &frameMargins);
}

void NativeWindow::setMaximizeButtonHovered(const bool hovered)
{
    if (m_maximizeButtonHovered == hovered)
    {
        return;
    }
    m_maximizeButtonHovered = hovered;
    qCDebug(windowLog) << "maximize hover=" << hovered;
    emit maximizeButtonHoveredChanged();
}

void NativeWindow::setMaximizeButtonPressed(const bool pressed)
{
    if (m_maximizeButtonPressed == pressed)
    {
        return;
    }
    m_maximizeButtonPressed = pressed;
    emit maximizeButtonPressedChanged();
}

} // namespace ztermy
