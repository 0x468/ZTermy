#pragma once

#include "core/windowing/WindowPresenter.h"
#include "platform/windows/NativeWindow.h"

#include <QColor>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QQuickItem>
#include <QTimer>
#include <QWindow>

#include <chrono>

namespace ztermy::ui
{
inline void processWindowEventsFor(const std::chrono::milliseconds duration)
{
    QEventLoop loop;
    QTimer::singleShot(duration, &loop, &QEventLoop::quit);
    loop.exec();
}

// The placement flag Windows consults when a minimized window comes back; a
// forgotten maximize state surfaces here before it is visible anywhere else.
[[nodiscard]] inline bool restoresToMaximized(const HWND handle)
{
    WINDOWPLACEMENT placement{.length = sizeof(WINDOWPLACEMENT)};
    return GetWindowPlacement(handle, &placement) != FALSE && (placement.flags & WPF_RESTORETOMAXIMIZED) != 0;
}

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
        [&window, handle] {
            return IsIconic(handle) != FALSE && window.windowStates().testFlag(Qt::WindowMaximized)
                   && restoresToMaximized(handle);
        },
        2s);
    qInfo() << "Window state smoke: minimize keeps maximized state:" << minimizedKeepsMaximize
            << "iconic=" << (IsIconic(handle) != FALSE) << "states=" << window.windowStates()
            << "restoreToMaximized=" << restoresToMaximized(handle);

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

// ADR 0120: the native material shows through the chrome and the terminal
// workspace only; content pages, panels and controls stay opaque under every
// backdrop, so lowering the backdrop opacity can never wash out a page.
struct SurfaceAlphas
{
    int root = -1;
    int chrome = -1;
    int workspace = -1;
    int content = -1;
    int panel = -1;
    int elevated = -1;
    int control = -1;
    int field = -1;

    [[nodiscard]] bool contentOpaque() const
    {
        return content == 255 && panel == 255 && elevated == 255 && control == 255 && field == 255;
    }

    [[nodiscard]] bool terminalMaterialTint(const int backgroundAlpha) const
    {
        return root == backgroundAlpha && chrome == 0 && workspace == backgroundAlpha && contentOpaque();
    }
};

[[nodiscard]] inline SurfaceAlphas sampleSurfaceAlphas(const NativeWindow &window, const char *state)
{
    const QQuickItem *rootObject = window.rootObject();
    const auto alpha = [rootObject](const char *propertyName) {
        return rootObject == nullptr ? -1 : rootObject->property(propertyName).value<QColor>().alpha();
    };
    const SurfaceAlphas alphas{.root = alpha("backgroundColor"),
                               .chrome = alpha("chromeColor"),
                               .workspace = alpha("workspaceColor"),
                               .content = alpha("contentColor"),
                               .panel = alpha("panelColor"),
                               .elevated = alpha("elevatedColor"),
                               .control = alpha("controlColor"),
                               .field = alpha("fieldColor")};
    qInfo() << "Window appearance surface alphas" << state << "root=" << alphas.root << "chrome=" << alphas.chrome
            << "workspace=" << alphas.workspace << "content=" << alphas.content << "panel=" << alphas.panel
            << "elevated=" << alphas.elevated << "control=" << alphas.control << "field=" << alphas.field;
    return alphas;
}
} // namespace ztermy::ui
