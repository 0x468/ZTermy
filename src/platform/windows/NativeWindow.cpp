#include "platform/windows/NativeWindow.h"

#include "platform/windows/WindowHitTest.h"

#include <QEvent>
#include <QQmlContext>
#include <QScreen>

#include <Windows.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>

namespace
{

constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr DWORD kDwmWindowCornerPreference = 33;
constexpr DWORD kDwmSystemBackdropType = 38;
constexpr int kDwmWindowCornerRound = 2;
constexpr int kDwmSystemBackdropMainWindow = 2;

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
    setTitle(QStringLiteral("ztermy"));
    setResizeMode(QQuickView::SizeRootObjectToView);
    setMinimumSize(QSize(500, 360));
    resize(1180, 760);
    setColor(Qt::transparent);
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
                *result = 0;
                return true;
            }
            break;

        case WM_NCHITTEST:
        {
            LRESULT dwmResult = 0;
            DwmDefWindowProc(windowHandle, nativeMessage->message, nativeMessage->wParam, nativeMessage->lParam,
                             &dwmResult);

            POINT clientPoint{
                .x = GET_X_LPARAM(nativeMessage->lParam),
                .y = GET_Y_LPARAM(nativeMessage->lParam),
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

            const auto area = windowing::classifyHitTest({.x = clientPoint.x, .y = clientPoint.y},
                                                         {.width = clientRect.right, .height = clientRect.bottom},
                                                         metrics, maximized());

            *result = toNativeHitArea(area);
            return true;
        }

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

                *result =
                    DefWindowProcW(windowHandle, nativeMessage->message, nativeMessage->wParam, nativeMessage->lParam);
                return true;
            }
            break;
        }

        case WM_NCMOUSELEAVE:
            setMaximizeButtonHovered(false);
            setMaximizeButtonPressed(false);
            *result =
                DefWindowProcW(windowHandle, nativeMessage->message, nativeMessage->wParam, nativeMessage->lParam);
            return true;

        case WM_NCLBUTTONDOWN:
            if (nativeMessage->wParam == HTMAXBUTTON)
            {
                setMaximizeButtonPressed(true);
                *result =
                    DefWindowProcW(windowHandle, nativeMessage->message, nativeMessage->wParam, nativeMessage->lParam);
                setMaximizeButtonPressed(false);
                return true;
            }
            break;

        case WM_NCLBUTTONUP:
            setMaximizeButtonPressed(false);
            if (nativeMessage->wParam == HTMAXBUTTON)
            {
                *result =
                    DefWindowProcW(windowHandle, nativeMessage->message, nativeMessage->wParam, nativeMessage->lParam);
                return true;
            }
            break;

        case WM_GETMINMAXINFO:
        {
            const HMONITOR monitor = MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST);
            MONITORINFO monitorInfo{.cbSize = sizeof(MONITORINFO)};

            if (GetMonitorInfoW(monitor, &monitorInfo) != FALSE)
            {
                auto *minMaxInfo =
                    reinterpret_cast<MINMAXINFO *>(nativeMessage->lParam); // NOLINT(performance-no-int-to-ptr)
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

void NativeWindow::configureNativeWindow()
{
    const auto windowHandle = reinterpret_cast<HWND>(winId()); // NOLINT(performance-no-int-to-ptr)
    LONG_PTR style = GetWindowLongPtrW(windowHandle, GWL_STYLE);
    style |= WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
    SetWindowLongPtrW(windowHandle, GWL_STYLE, style);

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
