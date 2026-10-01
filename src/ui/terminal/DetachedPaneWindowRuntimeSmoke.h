#pragma once

#include "application/AppController.h"
#include "platform/windows/DetachedWindowNativeFrame.h"
#include "platform/windows/NativeWindow.h"
#include "ui/WindowStateRuntimeSmoke.h"

#include <QCursor>
#include <QDir>
#include <QPointer>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <vector>

namespace ztermy::ui
{
[[nodiscard]] inline QQuickItem *detachedVisualQuickItem(QQuickItem *root, const QString &name)
{
    std::vector<QQuickItem *> pending{root};
    for (std::size_t index = 0; index < pending.size(); ++index)
    {
        QQuickItem *candidate = pending[index];
        if (candidate->objectName() == name && candidate->isVisible())
            return candidate;
        const auto children = candidate->childItems();
        pending.insert(pending.end(), children.cbegin(), children.cend());
    }
    return nullptr;
}

inline bool verifyDetachedCaptionStateRoundTrip(QQuickWindow &detached, const QString &outputDirectory)
{
    using namespace std::chrono_literals;
    const auto previousPointer = QCursor::pos();
    const auto restorePointer = qScopeGuard([&] {
        QCursor::setPos(previousPointer);
    });
    const auto handle = reinterpret_cast<HWND>(detached.winId()); // NOLINT(performance-no-int-to-ptr)
    POINT oldButton{.x = qRound((detached.width() - 48) * detached.devicePixelRatio()),
                    .y = qRound(16 * detached.devicePixelRatio())};
    if (!ClientToScreen(handle, &oldButton))
        return false;
    const LPARAM oldPosition = MAKELPARAM(oldButton.x, oldButton.y);
    detached.setProperty("nativeMaximizeButtonHovered", true);
    QCursor::setPos(detached.mapToGlobal(QPoint{detached.width() / 2, 200}));
    detached.setProperty("windowControlsVisible", false);
    const bool dismissed = settleWindowUntil(
        [&detached] {
            return !detached.property("windowControlsVisible").toBool()
                   && !detached.property("windowChromeInteractive").toBool();
        },
        2s);
    qInfo() << "Detached dismissal: settled, requested, interactive, native hover:" << dismissed
            << detached.property("windowControlsVisible") << detached.property("windowChromeInteractive")
            << detached.property("nativeMaximizeButtonHovered");
    const bool hiddenClient = SendMessageW(handle, WM_NCHITTEST, 0, oldPosition) == HTCLIENT;
    const bool wasMaximized = IsZoomed(handle) != FALSE;
    // Even stale native hover/click messages must not resurrect hidden chrome.
    qintptr ignoredResult = 0;
    for (const UINT kind : {WM_NCMOUSEMOVE, WM_NCLBUTTONDOWN, WM_NCLBUTTONUP})
    {
        const MSG stale{.hwnd = handle, .message = kind, .wParam = HTMAXBUTTON, .lParam = oldPosition};
        if (!windowing::handleDetachedWindowFrameMessage(detached, stale, &ignoredResult) && kind != WM_NCMOUSEMOVE)
            return false;
    }
    processWindowEventsFor(100ms);
    const bool inert = !detached.property("nativeMaximizeButtonHovered").toBool()
                       && !detached.property("nativeMaximizeButtonPressed").toBool()
                       && (IsZoomed(handle) != FALSE) == wasMaximized;
    QCursor::setPos(detached.mapToGlobal(QPoint{detached.width() / 2, 6}));
    detached.setProperty("windowControlsVisible", true);
    processWindowEventsFor(100ms);
    qInfo() << "Hidden caption: old button is client, stale hover/click is inert:" << hiddenClient << inert;
    if (!hiddenClient || !inert)
        return false;
    const auto clickCaption = [&detached, handle](const QString &kind) {
        QCursor::setPos(detached.mapToGlobal(QPoint{detached.width() / 2, 6}));
        detached.setProperty("windowControlsVisible", true);
        processWindowEventsFor(200ms);
        const QString name = QStringLiteral("detachedWindowAction-") + kind;
        auto *button = detachedVisualQuickItem(detached.contentItem(), name);
        if (button == nullptr)
            return false;
        if (kind == QStringLiteral("maximize"))
        {
            const QPointF center = button->mapToScene({button->width() / 2, button->height() / 2});
            POINT screen{.x = qRound(center.x() * detached.devicePixelRatio()),
                         .y = qRound(center.y() * detached.devicePixelRatio())};
            if (!ClientToScreen(handle, &screen))
                return false;
            const LPARAM position = MAKELPARAM(screen.x, screen.y);
            const bool nativeHit = SendMessageW(handle, WM_NCHITTEST, 0, position) == HTMAXBUTTON;
            PostMessageW(handle, WM_NCLBUTTONDOWN, HTMAXBUTTON, position);
            processWindowEventsFor(50ms);
            const bool pressForwarded = detached.property("nativeMaximizeButtonPressed").toBool();
            PostMessageW(handle, WM_NCLBUTTONUP, HTMAXBUTTON, position);
            processWindowEventsFor(50ms);
            const bool released = !detached.property("nativeMaximizeButtonPressed").toBool();
            qInfo() << "Detached maximize exposes native Snap hit target:" << nativeHit << pressForwarded << released;
            return nativeHit && pressForwarded && released;
        }
        return QMetaObject::invokeMethod(button, "activated");
    };

    const bool maximized = clickCaption(QStringLiteral("maximize"))
                           && settleWindowUntil(
                               [handle] {
                                   return IsZoomed(handle) != FALSE;
                               },
                               3s);
    qInfo() << "Detached caption maximize:" << maximized;
    QCursor::setPos(detached.mapToGlobal(QPoint{detached.width() / 2, 6}));
    const auto capture = detached.contentItem()->grabToImage();
    const bool captured =
        capture
        && settleWindowUntil(
            [&] {
                return !capture->image().isNull();
            },
            3s)
        && capture->image().save(QDir(outputDirectory).filePath(QStringLiteral("detached-pane-maximized.png")));
    const bool minimizedKeepsMaximize = clickCaption(QStringLiteral("minimize"))
                                        && settleWindowUntil(
                                            [&detached, handle] {
                                                return IsIconic(handle) != FALSE
                                                       && detached.windowStates().testFlag(Qt::WindowMaximized)
                                                       && restoresToMaximized(handle);
                                            },
                                            2s);
    qInfo() << "Detached caption minimize keeps maximized state:" << minimizedKeepsMaximize;
    windowing::present(detached);
    const bool presentedMaximized = settleWindowUntil(
        [handle] {
            return IsIconic(handle) == FALSE && IsZoomed(handle) != FALSE;
        },
        2s);
    const bool restored = clickCaption(QStringLiteral("maximize"))
                          && settleWindowUntil(
                              [handle] {
                                  return IsZoomed(handle) == FALSE && IsIconic(handle) == FALSE;
                              },
                              2s);
    qInfo() << "Detached caption restore:" << restored;
    return maximized && captured && minimizedKeepsMaximize && presentedMaximized && restored;
}

inline bool verifyDetachedWindowReattach(NativeWindow &window, AppController &controller, QQuickWindow &detached,
                                         const QString &detachedWorkspaceId)
{
    QPointer<QQuickWindow> guard{&detached};
    const auto layout = controller.terminalWorkspace(detachedWorkspaceId).value(QStringLiteral("root"));
    auto *button = detachedVisualQuickItem(detached.contentItem(), QStringLiteral("detachedReattachAllButton"));
    const bool triggered = button && QMetaObject::invokeMethod(button, "activated");
    processWindowEventsFor(std::chrono::milliseconds{400});
    const auto returned = controller.terminalWorkspace(detachedWorkspaceId);
    const bool passed = triggered && guard.isNull()
                        && returned.value(QStringLiteral("windowId")) == QStringLiteral("main")
                        && returned.value(QStringLiteral("root")) == layout
                        && window.rootObject()->property("mainWorkspaceId") == detachedWorkspaceId;
    qInfo() << "Explicit reattach preserves layout, selects returned Tab and closes detached window:" << passed;
    return passed;
}
} // namespace ztermy::ui
