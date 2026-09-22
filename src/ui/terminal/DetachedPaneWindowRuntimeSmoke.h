#pragma once

#include "application/AppController.h"
#include "platform/windows/NativeWindow.h"
#include "ui/WindowStateRuntimeSmoke.h"

#include <QCursor>
#include <QDir>
#include <QPointer>
#include <QQuickItem>
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

inline bool verifyDetachedCaptionStateRoundTrip(QQuickWindow &detached, const QString &paneId,
                                                const QString &outputDirectory)
{
    using namespace std::chrono_literals;
    const auto handle = reinterpret_cast<HWND>(detached.winId()); // NOLINT(performance-no-int-to-ptr)
    const auto clickCaption = [&detached, &paneId, handle](const QString &kind) {
        const QString name = QStringLiteral("detachedWindowAction-") + kind + QLatin1Char('-') + paneId;
        auto *button = detachedVisualQuickItem(detached.contentItem(), name);
        if (button == nullptr)
            return false;
        if (kind == QStringLiteral("maximize"))
        {
            const QPointF center = button->mapToScene({button->width() / 2, button->height() / 2});
            const QPoint screen = detached.mapToGlobal(center.toPoint());
            const LPARAM position = MAKELPARAM(screen.x(), screen.y());
            const bool nativeHit = SendMessageW(handle, WM_NCHITTEST, 0, position) == HTMAXBUTTON;
            PostMessageW(handle, WM_NCLBUTTONDOWN, HTMAXBUTTON, position);
            processWindowEventsFor(50ms);
            const bool pressForwarded = detached.property("nativeMaximizeButtonPressed").toBool();
            PostMessageW(handle, WM_NCLBUTTONUP, HTMAXBUTTON, position);
            processWindowEventsFor(50ms);
            const bool released = !detached.property("nativeMaximizeButtonPressed").toBool();
            qInfo() << "Detached maximize exposes native Snap hit target:" << nativeHit << pressForwarded << released;
            return nativeHit && released;
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
    const bool captured =
        detached.grabWindow().save(QDir(outputDirectory).filePath(QStringLiteral("detached-pane-maximized.png")));
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

inline bool verifyDetachedWindowTabMerge(NativeWindow &window, AppController &controller, QQuickWindow &detached,
                                         QQuickItem &targetTab, const QString &detachedWorkspaceId,
                                         const QString &targetWorkspaceId)
{
    QPointer<QQuickWindow> detachedGuard{&detached};
    const QPointF targetScene = targetTab.mapToScene({targetTab.width() / 2, targetTab.height() / 2});
    const QPoint targetGlobal = window.mapToGlobal(targetScene.toPoint());
    const auto detachedHandle = reinterpret_cast<HWND>(detached.winId()); // NOLINT(performance-no-int-to-ptr)
    detached.setProperty("paneDockMoveActive", true);
    SetCursorPos(targetGlobal.x(), targetGlobal.y());
    const bool cursorWarped = (QCursor::pos() - targetGlobal).manhattanLength() <= 2;
    if (cursorWarped)
        SendMessageW(detachedHandle, WM_MOVE, 0, 0);
    else
        emit window.detachedWindowMoving(&detached, targetGlobal);
    processWindowEventsFor(std::chrono::milliseconds{250});
    const auto *coordinator = window.rootObject()->findChild<QObject *>(QStringLiteral("terminalWindowCoordinator"));
    const QString targetMode =
        coordinator ? coordinator->property("dropTarget").toMap().value(QStringLiteral("mode")).toString() : QString{};
    const bool moveForwarded = coordinator && coordinator->property("movingWindow").value<QObject *>() == &detached;
    const bool dockPreviewVisible = qFuzzyCompare(detached.opacity(), 0.72);
    qInfo() << "Native detached-window drag resolves tab target:" << targetMode << "moveForwarded=" << moveForwarded
            << "cursorWarped=" << cursorWarped;
    SendMessageW(detachedHandle, WM_EXITSIZEMOVE, 0, 0);
    const bool moveFinished = !detached.property("paneDockMoveActive").toBool();
    processWindowEventsFor(std::chrono::milliseconds{250});
    const bool merged =
        controller.terminalWorkspace(detachedWorkspaceId).isEmpty()
        && controller.terminalWorkspace(targetWorkspaceId).value(QStringLiteral("paneCount")).toInt() == 2;
    qInfo() << "Native detached-window drag merges into a tab:" << merged;
    return targetMode == QStringLiteral("merge") && moveForwarded && dockPreviewVisible && merged && moveFinished
           && detachedGuard.isNull();
}
} // namespace ztermy::ui
