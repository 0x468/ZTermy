#pragma once

#include "core/windowing/WindowPresenter.h"
#include "platform/windows/NativeWindow.h"
#include "ui/terminal/TerminalPaneRuntimeSmoke.h"

#include <QDebug>
#include <QElapsedTimer>

#include <chrono>

namespace ztermy::ui
{
inline void showForRuntimeSmoke(QWindow &window)
{
    window.show();
}

inline void maximizeForRuntimeSmoke(QWindow &window)
{
    window.showMaximized();
}

inline void restoreForRuntimeSmoke(QWindow &window)
{
    window.showNormal();
}

template <typename Predicate>
[[nodiscard]] inline bool settleWindowUntil(Predicate predicate, const std::chrono::milliseconds timeout)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < timeout.count())
    {
        if (predicate())
            return true;
        processWindowEventsFor(std::chrono::milliseconds{25});
    }
    return predicate();
}

// Drives the real window through maximize -> minimize -> present -> restore.
// Besides the Qt flags it checks the native placement Windows consults when the
// taskbar or a later activation brings the window back, which is where a
// forgotten maximize state actually surfaces.
[[nodiscard]] inline bool verifyWindowStateRoundTrip(NativeWindow &window)
{
    using namespace std::chrono_literals;
    // The smoke drives absolute states on purpose; product code goes through ztermy::windowing.
    window.show();
    if (!settleWindowUntil(
            [&window] {
                return window.isVisible();
            },
            2s))
    {
        qWarning() << "Window state smoke: window did not become visible";
        return false;
    }
    const auto handle = reinterpret_cast<HWND>(window.winId()); // NOLINT(performance-no-int-to-ptr)
    const auto restoresToMaximized = [handle] {
        WINDOWPLACEMENT placement{.length = sizeof(WINDOWPLACEMENT)};
        return GetWindowPlacement(handle, &placement) != FALSE && (placement.flags & WPF_RESTORETOMAXIMIZED) != 0;
    };

    window.showMaximized();
    const bool maximized = settleWindowUntil(
        [&window] {
            return window.maximized() && window.maximizedClientMatchesWorkArea();
        },
        3s);
    qInfo() << "Window state smoke: maximized client matches work area:" << maximized
            << "maximized=" << window.maximized() << "workAreaMatches=" << window.maximizedClientMatchesWorkArea();

    windowing::minimize(window);
    const bool minimizedKeepsMaximize = settleWindowUntil(
        [&window, handle, &restoresToMaximized] {
            return IsIconic(handle) != FALSE && window.windowStates().testFlag(Qt::WindowMaximized)
                   && restoresToMaximized();
        },
        2s);
    qInfo() << "Window state smoke: minimize keeps maximized state:" << minimizedKeepsMaximize
            << "iconic=" << (IsIconic(handle) != FALSE) << "states=" << window.windowStates()
            << "restoreToMaximized=" << restoresToMaximized();

    windowing::present(window);
    const bool presentedMaximized = settleWindowUntil(
        [&window, handle] {
            return IsIconic(handle) == FALSE && IsZoomed(handle) != FALSE && window.maximized();
        },
        2s);
    qInfo() << "Window state smoke: present restores maximized window:" << presentedMaximized
            << "iconic=" << (IsIconic(handle) != FALSE) << "zoomed=" << (IsZoomed(handle) != FALSE);

    windowing::toggleMaximize(window);
    const bool restored = settleWindowUntil(
        [&window, handle] {
            return !window.maximized() && IsZoomed(handle) == FALSE;
        },
        2s);
    qInfo() << "Window state smoke: toggle restores normal geometry:" << restored;

    return maximized && minimizedKeepsMaximize && presentedMaximized && restored;
}
} // namespace ztermy::ui
