#pragma once

#include "core/windowing/WindowPresenter.h"
#include "platform/windows/NativeWindow.h"
#include "ui/WindowStatusRuntimeSmoke.h"

#include <QColor>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QPointer>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QScopeGuard>
#include <QScreen>
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

[[nodiscard]] inline bool verifyWindowNotificationRouting(NativeWindow &main, QWindow &detached)
{
    using namespace std::chrono_literals;
    auto *mainToast = main.rootObject()->findChild<QObject *>(QStringLiteral("terminalNotificationToast"),
                                                              Qt::FindDirectChildrenOnly);
    auto *detachedToast = detached.findChild<QObject *>(QStringLiteral("terminalNotificationToast"));
    auto *controller = main.rootObject()->property("controller").value<QObject *>();
    if (!mainToast || !detachedToast || !controller)
        return false;
    auto *focused = QGuiApplication::focusWindow();
    bool passed = true;
    for (const bool toDetached : {true, false})
    {
        const QVariantMap notification{
            {QStringLiteral("windowId"), toDetached ? QStringLiteral("restore-window") : QStringLiteral("main")},
            {QStringLiteral("title"), QStringLiteral("Routing check")},
            {QStringLiteral("message"), QStringLiteral("Plain terminal notification")}};
        const bool delivered =
            QMetaObject::invokeMethod(controller, "terminalNotificationRequested", Q_ARG(QVariantMap, notification));
        processWindowEventsFor(300ms);
        passed = passed && delivered && mainToast->property("visible").toBool() == !toDetached
                 && detachedToast->property("visible").toBool() == toDetached
                 && QGuiApplication::focusWindow() == focused;
        QMetaObject::invokeMethod(mainToast, "close");
        QMetaObject::invokeMethod(detachedToast, "close");
        processWindowEventsFor(300ms);
    }
    qInfo() << "Window-local notifications without focus change:" << passed;
    return passed;
}

[[nodiscard]] inline QQuickItem *findWindowSmokeItem(QQuickItem *root, const QString &name);

[[nodiscard]] inline bool captureWindowSmokeItem(QQuickItem *item, const QString &name)
{
    const auto arguments = QCoreApplication::arguments();
    const auto dataIndex = arguments.indexOf(QStringLiteral("--data-dir"));
    if (!item || dataIndex < 0 || dataIndex + 1 >= arguments.size())
        return false;
    QPointer<QQuickItem> guardedItem(item);
    const auto objectName = item->objectName();
    if (auto *targetWindow = item->window())
    {
        windowing::present(*targetWindow);
        processWindowEventsFor(std::chrono::milliseconds{250});
        if (!targetWindow->isExposed())
        {
            // Start-Process -WindowStyle Hidden can consume the first native
            // show request despite Qt already reporting the window as visible.
            targetWindow->hide();
            windowing::present(*targetWindow);
            processWindowEventsFor(std::chrono::milliseconds{250});
        }
        qInfo() << "Profile capture window exposed:" << targetWindow->isExposed();
        if (!objectName.isEmpty())
            guardedItem = findWindowSmokeItem(targetWindow->contentItem(), objectName);
    }
    qInfo() << "Profile icon capture" << name << guardedItem.data() << (guardedItem ? guardedItem->size() : QSizeF{});
    if (!guardedItem)
        return false;
    const auto capture = guardedItem->grabToImage();
    return capture
           && settleWindowUntil(
               [&] {
                   return !capture->image().isNull();
               },
               std::chrono::seconds{3})
           && capture->image().save(QDir(arguments[dataIndex + 1]).filePath(name));
}

[[nodiscard]] inline bool
verifyHostProfileIconPicker(NativeWindow &window, const QString &captureName = QStringLiteral("profile-icon-menu.png"))
{
    using namespace std::chrono_literals;
    auto *pane = window.rootObject()->findChild<QObject *>(QStringLiteral("hostConnectionPane"));
    auto *field = window.rootObject()->findChild<QObject *>(QStringLiteral("hostName"));
    auto *button = window.rootObject()->findChild<QObject *>(QStringLiteral("hostProfileIconButton"));
    auto *menu = window.rootObject()->findChild<QObject *>(QStringLiteral("hostProfileIconMenu"));
    if (!pane || !field || !button || !menu || !QMetaObject::invokeMethod(pane, "beginNewProfile"))
        return false;
    processWindowEventsFor(300ms);
    if (!QMetaObject::invokeMethod(button, "clicked"))
        return false;
    processWindowEventsFor(300ms);
    auto *content = qvariant_cast<QQuickItem *>(menu->property("contentItem"));
    auto *choice = findWindowSmokeItem(content, QStringLiteral("hostProfileIcon-brand-archlinux"));
    const bool visible = menu->property("visible").toBool() && choice && choice->isVisible();
    const bool captured = captureWindowSmokeItem(content ? content->parentItem() : nullptr, captureName);
    const bool selected = choice && QMetaObject::invokeMethod(choice, "click")
                          && field->property("profileIcon").toString() == QStringLiteral("brand-archlinux");
    QMetaObject::invokeMethod(menu, "close");
    const bool reset = QMetaObject::invokeMethod(pane, "clearEditor")
                       && field->property("profileIcon").toString() == QStringLiteral("terminal");
    qInfo() << "Profile icon grid: visible, distribution selection, new-profile reset:" << visible << selected << reset;
    return visible && captured && selected && reset;
}

[[nodiscard]] inline QQuickItem *findWindowSmokeItem(QQuickItem *root, const QString &name)
{
    if (!root || root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = findWindowSmokeItem(child, name))
            return found;
    return nullptr;
}

[[nodiscard]] inline bool verifySavedWindowStartup(NativeWindow &window)
{
    using namespace std::chrono_literals;
    windowing::reveal(window);
    processWindowEventsFor(600ms);
    QWindow *detached = nullptr;
    for (auto *candidate : QGuiApplication::allWindows())
        if (candidate->property("ownerWindowId").toString() == QStringLiteral("restore-window"))
            detached = candidate;
    const auto mainHandle = reinterpret_cast<HWND>(window.winId()); // NOLINT(performance-no-int-to-ptr)
    const bool removedScreen = QCoreApplication::arguments().contains(QStringLiteral("--removed-screen-startup"));
    const bool mainBounds = removedScreen ? window.screen()->availableGeometry().contains(window.geometry())
                                                && window.size() == QSize(840, 540)
                                          : window.geometry() == QRect(50, 60, 840, 540);
    const bool mainRestored =
        mainBounds && !IsZoomed(mainHandle)
        && window.rootObject()->property("mainWorkspaceId").toString() == QStringLiteral("main-check");
    bool detachedRestored = false;
    if (detached)
    {
        const auto handle = reinterpret_cast<HWND>(detached->winId()); // NOLINT(performance-no-int-to-ptr)
        detachedRestored = detached->isVisible() && IsZoomed(handle)
                           && detached->property("workspaceId").toString() == QStringLiteral("detached-selected");
        if (detachedRestored)
        {
            windowing::toggleMaximize(*detached);
            processWindowEventsFor(300ms);
            const bool normalBounds = detached->size() == QSize(800, 520)
                                      && detached->screen()->availableGeometry().contains(detached->geometry());
            qInfo() << "Detached restored normal bounds:" << detached->geometry() << normalBounds;
            detachedRestored = normalBounds && !IsZoomed(handle);
            windowing::toggleMaximize(*detached);
            processWindowEventsFor(300ms);
            detachedRestored = detachedRestored && IsZoomed(handle);
        }
    }
    qInfo() << "Window restore startup: main bounds/selection, detached native maximum/selection:" << mainRestored
            << detachedRestored;
    auto *detachedQuick = qobject_cast<QQuickWindow *>(detached);
    auto *tabBar =
        detachedQuick ? findWindowSmokeItem(detachedQuick->contentItem(), QStringLiteral("detachedTabBar")) : nullptr;
    const bool hiddenByDefault = tabBar && !tabBar->isVisible() && tabBar->height() == 0;
    const auto selectionBeforeToggle = detached ? detached->property("workspaceId") : QVariant{};
    const bool toggled = detached && QMetaObject::invokeMethod(detached, "toggleTabBar");
    processWindowEventsFor(100ms);
    const bool shown = toggled && tabBar && tabBar->isVisible() && tabBar->height() == 32;
    if (detached)
        QMetaObject::invokeMethod(detached, "toggleTabBar");
    processWindowEventsFor(100ms);
    const bool hiddenAgain = tabBar && !tabBar->isVisible() && tabBar->height() == 0
                             && detached->property("workspaceId") == selectionBeforeToggle;
    qInfo() << "Detached tab chrome: hidden initially, shown, hidden without changing selection:" << hiddenByDefault
            << shown << hiddenAgain;
    if (detached)
        QMetaObject::invokeMethod(detached, "toggleTabBar");
    processWindowEventsFor(100ms);
    auto *mainTab = findWindowSmokeItem(window.contentItem(), QStringLiteral("workspaceTitle-main-check"));
    auto *detachedTab = detachedQuick ? findWindowSmokeItem(detachedQuick->contentItem(),
                                                            QStringLiteral("workspaceTitle-detached-selected"))
                                      : nullptr;
    const bool profileIcons = mainTab && detachedTab
                              && mainTab->property("iconName").toString() == QStringLiteral("security")
                              && detachedTab->property("iconName").toString() == QStringLiteral("security");
    qInfo() << "Profile icon reaches main and detached Tab delegates:" << profileIcons;
    const bool capturedIcons =
        captureWindowSmokeItem(mainTab, QStringLiteral("profile-icon-main.png"))
        && captureWindowSmokeItem(
            findWindowSmokeItem(detachedQuick->contentItem(), QStringLiteral("workspaceTitle-detached-selected")),
            QStringLiteral("profile-icon-detached.png"));
    const bool statusPresentation = !QCoreApplication::arguments().contains(QStringLiteral("--status-visual-smoke"))
                                    || (detachedQuick && verifyWindowStatusPresentation(window, *detachedQuick));
    const bool snapCaptured = !QCoreApplication::arguments().contains(QStringLiteral("--snap-layout-capture"))
                              || (detachedQuick && captureNativeSnapFlyout(*detachedQuick));
    return hiddenByDefault && shown && hiddenAgain && mainRestored && detachedRestored && profileIcons && capturedIcons
           && statusPresentation && snapCaptured && verifyWindowNotificationRouting(window, *detached)
           && verifyHostProfileIconPicker(window);
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

    auto *caption = window.rootObject()->findChild<QQuickItem *>(QStringLiteral("titleControls"));
    const bool normalCaptionCaptured = captureWindowSmokeItem(caption, QStringLiteral("caption-normal.png"));
    window.showMaximized();
    const bool maximized = settleWindowUntil(
        [&window] {
            return window.maximized() && window.maximizedClientMatchesWorkArea();
        },
        3s);
    qInfo() << "Window state smoke: maximized client matches work area:" << maximized
            << "maximized=" << window.maximized() << "workAreaMatches=" << window.maximizedClientMatchesWorkArea();
    const bool maximizedCaptionCaptured = captureWindowSmokeItem(caption, QStringLiteral("caption-maximized.png"));

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

    return normalCaptionCaptured && maximizedCaptionCaptured && maximized && minimizedKeepsMaximize
           && presentedMaximized && restored;
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
