#pragma once

#include "application/AppController.h"
#include "platform/windows/NativeWindow.h"
#include "ui/WindowStateRuntimeSmoke.h"
#include "ui/terminal/TerminalItem.h"

#include <QDir>
#include <QGuiApplication>
#include <QQuickItem>
#include <algorithm>
#include <vector>

#include "ui/terminal/TerminalPointerRuntimeSmoke.h"

namespace ztermy::ui
{
[[nodiscard]] inline QQuickItem *visualQuickItem(QQuickItem *rootObject, const char *objectName)
{
    if (rootObject == nullptr)
    {
        return nullptr;
    }
    const QString expectedName = QString::fromLatin1(objectName);
    std::vector<QQuickItem *> pending{rootObject};
    QQuickItem *fallback = nullptr;
    for (std::size_t index = 0; index < pending.size(); ++index)
    {
        QQuickItem *candidate = pending[index];
        if (candidate->objectName() == expectedName)
        {
            fallback = fallback == nullptr ? candidate : fallback;
            if (candidate->isVisible())
            {
                return candidate;
            }
        }
        const QList<QQuickItem *> children = candidate->childItems();
        pending.insert(pending.end(), children.cbegin(), children.cend());
    }
    return fallback;
}

inline void settleWindowLayout(QQuickWindow &window)
{
    // An occluded smoke window may not receive render frames. Explicitly render
    // while animations run before asserting polished item geometry.
    for (int frame = 0; frame < 6; ++frame)
    {
        window.requestUpdate();
        static_cast<void>(window.grabWindow());
        processWindowEventsFor(std::chrono::milliseconds{50});
    }
    static_cast<void>(window.grabWindow());
}

inline bool terminalWorkspaceViewsReady(NativeWindow &window, AppController &controller)
{
    const auto workspace = controller.activeTerminalWorkspace();
    const auto paneId = workspace.value(QStringLiteral("activePaneId")).toString();
    return workspace.value(QStringLiteral("paneCount")).toInt() == 2
           && window.findChild<TerminalItem *>(QStringLiteral("terminalViewport-") + paneId) != nullptr;
}
inline bool verifyWorkbenchResizeWhileDragging(NativeWindow &window, AppController &controller)
{
    if (!controller.toggleTerminalWorkbench(QStringLiteral("history")))
        return false;
    processWindowEventsFor(std::chrono::milliseconds{350});
    auto *root = window.rootObject();
    auto *grip = window.findChild<QQuickItem *>(QStringLiteral("terminalWorkbenchResizeHandle"));
    auto *viewport = window.findChild<QQuickItem *>(QStringLiteral("terminalWorkspaceViewport"));
    if (!root || !grip || !viewport || controller.terminalTabs().isEmpty())
        return false;
    const qreal original =
        controller.terminalTabs().constFirst().toMap().value(QStringLiteral("workbenchWidth")).toReal();
    const QPointF start = grip->mapToScene({grip->width() / 2, grip->height() / 2});
    const QPointF end = start + QPointF{60, 0};
    sendMouse(window, start, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
    sendMouse(window, end, Qt::LeftButton, Qt::NoButton, QEvent::MouseMove);
    const qreal live = root->property("activeTerminalWorkbenchWidth").toReal();
    const bool liveResize =
        root->property("workbenchResizeInProgress").toBool() && qAbs(live - original - 60) < 2
        && qAbs(viewport->x() - live) < 2
        && qAbs(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("workbenchWidth")).toReal()
                - original)
               < 1;
    sendMouse(window, end, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
    const bool committed =
        !root->property("workbenchResizeInProgress").toBool()
        && qAbs(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("workbenchWidth")).toReal() - live)
               < 2;
    controller.closeTerminalWorkbench();
    processWindowEventsFor(std::chrono::milliseconds{350});
    qInfo() << "Workbench live resize and single commit:" << liveResize << committed;
    return liveResize && committed;
}
// In-process Qt regression check; uses an isolated smoke data directory and
inline bool verifyWholeTabMouseMerge(NativeWindow &window, AppController &controller, const QString &workspaceId,
                                     const QString &otherId, const QString &paneId)
{
    bool passed = true;
    controller.activateTerminalTab(otherId);
    settleWindowLayout(window);
    const QString destinationPaneId =
        controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    auto *sourceTab =
        visualQuickItem(window.rootObject(), (QStringLiteral("workspaceTitle-") + workspaceId).toLatin1().constData());
    auto *destinationView = window.findChild<TerminalItem *>(QStringLiteral("terminalViewport-") + destinationPaneId);
    if (sourceTab && destinationView)
    {
        const QPointF start = sourceTab->mapToScene({sourceTab->width() / 2, sourceTab->height() / 2});
        const QPointF end =
            destinationView->mapToScene({destinationView->width() / 2, destinationView->height() * 0.1});
        dragMouse(window, start, end, 12, std::chrono::milliseconds{40});
        processWindowEventsFor(std::chrono::milliseconds{250});
        const bool unchanged = controller.terminalWorkspace(workspaceId).value(QStringLiteral("paneCount")).toInt() == 2
                               && controller.terminalWorkspace(otherId).value(QStringLiteral("paneCount")).toInt() == 1;
        qInfo() << "Tab drag into terminal does not merge or detach workspaces:" << unchanged;
        passed = passed && unchanged;
        controller.closeTerminalTab(otherId);
    }
    else
    {
        qWarning() << "Workspace drag source or destination was not found";
        passed = false;
        controller.closeTerminalTab(otherId);
    }
    controller.activateTerminalPane(paneId);
    processWindowEventsFor(std::chrono::milliseconds{250});
    return passed;
}
// Drives a detached window through its own QML caption buttons:
// maximize -> minimize -> present -> restore, checking the same native state
// the main-window smoke checks, so the WindowControl path used by detached
// windows has the same evidence as the main title bar.
inline bool verifyDetachedCaptionStateRoundTrip(QQuickWindow &detached, const QString &paneId,
                                                const QString &outputDirectory)
{
    using namespace std::chrono_literals;
    const auto clickCaption = [&detached, &paneId](const QString &kind) {
        // Repeater delegates are not QObject children of the window; walk the items.
        const QString name = QStringLiteral("detachedWindowAction-") + kind + QLatin1Char('-') + paneId;
        auto *button = visualQuickItem(detached.contentItem(), name.toLatin1().constData());
        if (button == nullptr)
        {
            qWarning() << "Detached caption button not found:" << kind;
            return false;
        }
        clickMouse(detached, button->mapToScene({button->width() / 2, button->height() / 2}));
        return true;
    };
    const auto handle = reinterpret_cast<HWND>(detached.winId()); // NOLINT(performance-no-int-to-ptr)

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
    qInfo() << "Detached caption minimize keeps maximized state:" << minimizedKeepsMaximize
            << "iconic=" << (IsIconic(handle) != FALSE) << "states=" << detached.windowStates()
            << "restoreToMaximized=" << restoresToMaximized(handle);

    // Workspace activation presents detached windows through the same owner.
    windowing::present(detached);
    const bool presentedMaximized = settleWindowUntil(
        [handle] {
            return IsIconic(handle) == FALSE && IsZoomed(handle) != FALSE;
        },
        2s);
    qInfo() << "Detached present restores maximized window:" << presentedMaximized;

    const bool restored = clickCaption(QStringLiteral("maximize"))
                          && settleWindowUntil(
                              [handle] {
                                  return IsZoomed(handle) == FALSE && IsIconic(handle) == FALSE;
                              },
                              2s);
    qInfo() << "Detached caption restore:" << restored;
    return maximized && captured && minimizedKeepsMaximize && presentedMaximized && restored;
}
// creates no remote sessions or additional terminal input.
inline bool verifyTerminalPaneWindowInteractions(NativeWindow &window, AppController &controller,
                                                 const QString &outputDirectory)
{
    auto *root = window.rootObject();
    if (!root)
        return false;
    const auto settle = [] {
        processWindowEventsFor(std::chrono::milliseconds{250});
    };
    const QString workspaceId = controller.activeTerminalTabId();
    const QString paneId = controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    QMetaObject::invokeMethod(root, "toggleTerminalPaneHeaders");
    settle();
    auto *pane = window.findChild<TerminalItem *>(QStringLiteral("terminalViewport-") + paneId);
    bool passed = pane && pane->y() >= 32;
    qInfo() << "Window transfer header geometry:" << passed;
    if (auto *action = window.findChild<QQuickItem *>(QStringLiteral("alwaysOnTopAction")))
    {
        synthesizeMouse(window, action->mapToScene(QPointF{action->width() / 2, action->height() / 2}), Qt::NoButton,
                        Qt::NoButton, QEvent::MouseMove);
        QCoreApplication::processEvents();
        const auto expected = action->property("feedbackColor").value<QColor>();
        const bool directHover =
            expected.alpha() > 0 && action->parentItem()->property("color").value<QColor>() == expected;
        qInfo() << "Title action enters final hover color immediately:" << directHover;
        passed = passed && directHover;
    }
    const auto layout = controller.activeTerminalWorkspace().value(QStringLiteral("root")).toMap();
    const QString firstId = layout.value(QStringLiteral("first")).toMap().value(QStringLiteral("id")).toString();
    const QString secondId = layout.value(QStringLiteral("second")).toMap().value(QStringLiteral("id")).toString();
    auto *header = window.findChild<QQuickItem *>(QStringLiteral("terminalPaneHeader-") + firstId);
    auto *targetPane = window.findChild<TerminalItem *>(QStringLiteral("terminalViewport-") + secondId);
    if (targetPane)
    {
        TerminalItem replacement;
        controller.attachTerminalViewport(secondId, &replacement);
        controller.activateTerminalPane(firstId);
        // Empty input checks activation routing without injecting shell input.
        targetPane->inputGenerated({});
        const bool staleIgnored =
            controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString() == firstId;
        replacement.inputGenerated({});
        const bool currentAccepted =
            controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString() == secondId;
        controller.detachTerminalViewport(secondId, &replacement);
        controller.attachTerminalViewport(secondId, targetPane);
        passed = passed && staleIgnored && currentAccepted;
        qInfo() << "Replaced viewport cannot activate or send input:" << staleIgnored << currentAccepted;
    }
    if (header && targetPane)
    {
        const bool nestedSelectionActions =
            window.findChildren<QObject *>(QStringLiteral("terminalSelectionSearchAction")).size() >= 2;
        qInfo() << "Recursive panes receive selection actions:" << nestedSelectionActions;
        passed = passed && nestedSelectionActions;

        passed = verifyInactivePaneSelection(window, controller, firstId, secondId, targetPane) && passed;

        clickMouse(window, header->mapToScene(QPointF(12, 16)));
        settle();
        const bool headerFocused =
            controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString() == firstId;
        qInfo() << "Pane header click focuses pane:" << headerFocused;
        passed = passed && headerFocused;
        header = window.findChild<QQuickItem *>(QStringLiteral("terminalPaneHeader-") + secondId);
        targetPane = window.findChild<TerminalItem *>(QStringLiteral("terminalViewport-") + firstId);
        if (!header || !targetPane)
            return false;
        const QPointF start = header->mapToScene(QPointF(12, 16));
        // Land in the upper band of the target viewport: the drop-target
        // resolver only recognizes viewports, and that band means a vertical
        // split placed before the target.
        const QPointF end = targetPane->mapToScene(QPointF(targetPane->width() / 2, targetPane->height() * 0.1));
        dragMouse(window, start, end, 8);
        settle();
        const auto moved = controller.activeTerminalWorkspace();
        const bool reordered =
            moved.value(QStringLiteral("paneCount")).toInt() == 2
            && moved.value(QStringLiteral("root")).toMap().value(QStringLiteral("orientation")).toString()
                   == QStringLiteral("vertical");
        qInfo() << "Pane header drag reorders existing sessions:" << reordered;
        passed = passed && reordered;
        controller.moveTerminalPane(firstId, secondId, QStringLiteral("horizontal"), false);
        controller.activateTerminalPane(paneId);
        settle();
    }
    else
        passed = false;
    QMetaObject::invokeMethod(root, "toggleTerminalPaneHeaders");
    QMetaObject::invokeMethod(root, "toggleTerminalPaneZoom", Q_ARG(QVariant, paneId));
    settle();
    passed = passed && root->property("zoomedTerminalPaneId").toString() == paneId;

    const QString otherId = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    settle();
    passed = passed && !otherId.isEmpty() && root->property("zoomedTerminalPaneId").toString().isEmpty()
             && !root->property("paneHeadersVisible").toBool();
    qInfo() << "Window transfer isolated zoom/header state:" << passed;
    QMetaObject::invokeMethod(root, "toggleTerminalPaneHeaders");
    settle();
    const QString singlePaneId = controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    auto *singleHeader = window.findChild<QQuickItem *>(QStringLiteral("terminalPaneHeader-") + singlePaneId);
    if (singleHeader)
    {
        const QPointF start = singleHeader->mapToScene({12, 16});
        const QPointF end{-30, start.y()};
        dragMouse(window, start, end, 8);
        settle();
        passed = passed && root->property("detachedTerminalPaneId").toString() == singlePaneId;
        qInfo() << "Window transfer single-pane detach:" << passed;
        QMetaObject::invokeMethod(root, "reattachTerminalPane");
        settle();
    }
    else
        passed = false;
    controller.activateTerminalTab(workspaceId);
    settle();
    passed = passed && root->property("zoomedTerminalPaneId").toString() == paneId;
    qInfo() << "Window transfer original zoom restored:" << passed;
    QMetaObject::invokeMethod(root, "toggleTerminalPaneZoom", Q_ARG(QVariant, paneId));
    QMetaObject::invokeMethod(root, "detachTerminalPane", Q_ARG(QVariant, paneId));
    settle();
    const QString detachedWorkspaceId = root->property("detachedTerminalWorkspaceId").toString();
    passed = passed && detachedWorkspaceId != workspaceId
             && controller.terminalWorkspace(workspaceId).value(QStringLiteral("paneCount")).toInt() == 1
             && controller.terminalWorkspace(detachedWorkspaceId).value(QStringLiteral("paneCount")).toInt() == 1;
    qInfo() << "Window transfer extracted model ownership:" << passed;

    QQuickWindow *detached = nullptr;
    for (auto *candidate : QGuiApplication::allWindows())
        if (candidate->objectName() == QStringLiteral("detachedTerminalWindow")
            && candidate->property("workspaceId").toString() == detachedWorkspaceId)
            detached = qobject_cast<QQuickWindow *>(candidate);
    passed = passed && detached && detached->isVisible() && !detached->transientParent();
    qInfo() << "Window transfer independent native window:" << passed;
    if (detached)
    {
        const auto *detachedHeader = detached->findChild<QQuickItem *>(QStringLiteral("terminalPaneHeader-") + paneId);
        const bool detachedHeaderVisible = detached->property("paneHeadersVisible").toBool() && detachedHeader
                                           && detachedHeader->isVisible() && detachedHeader->height() >= 32;
        qInfo() << "Detached pane title is visible by default:" << detachedHeaderVisible;
        passed = passed && detachedHeaderVisible;
        const auto *actions = detached->findChild<QQuickItem *>(QStringLiteral("terminalPaneActions-") + paneId);
        const QPointF toolbarOrigin = actions ? actions->mapToScene(QPointF{}) : QPointF{-1, -1};
        const bool controlsFlush = actions && qAbs(toolbarOrigin.y()) < 1
                                   && qAbs(toolbarOrigin.x() + actions->width() - detached->width()) < 1;
        qInfo() << "Detached window controls align with top and right edges:" << controlsFlush;
        passed = passed && controlsFlush;
        const auto handle = reinterpret_cast<HWND>(detached->winId()); // NOLINT(performance-no-int-to-ptr)
        passed = passed && GetWindow(handle, GW_OWNER) == nullptr
                 && (GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) == 0;
        const auto originalSize = detached->size();
        detached->resize(1100, 760);
        settle();
        passed =
            detached->grabWindow().save(QDir(outputDirectory).filePath(QStringLiteral("detached-pane-resized.png")))
            && passed;
        passed = verifyDetachedCaptionStateRoundTrip(*detached, paneId, outputDirectory) && passed;
        detached->resize(originalSize);
        controller.activateTerminalTab(otherId);
        settle();
        passed = passed && detached->isVisible()
                 && root->property("detachedTerminalWorkspaceId").toString() == detachedWorkspaceId;
    }
    QMetaObject::invokeMethod(root, "reattachTerminalPane");
    qInfo() << "Window transfer native material and reattachment:" << passed;
    passed = verifyWholeTabMouseMerge(window, controller, workspaceId, otherId, paneId) && passed;
    passed = verifyNestedPaneEdges(window, controller, outputDirectory) && passed;
    qInfo() << "Pane headers, per-workspace zoom, detached taskbar/resize regression:" << passed;
    return passed;
}
inline bool runWorkspaceTransferRuntimeSmoke(NativeWindow &window, AppController &controller,
                                             const QString &outputDirectory)
{
    if (controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt")).isEmpty()
        || !controller.splitActiveTerminal(QStringLiteral("horizontal"), true))
        return false;
    window.rootObject()->setProperty("currentPage", QStringLiteral("terminal"));
    settleWindowLayout(window);
    if (QCoreApplication::arguments().contains(QStringLiteral("--workspace-merge-only")))
    {
        const auto workspaceId = controller.activeTerminalTabId();
        const auto paneId = controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
        const auto otherId = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
        return verifyWholeTabMouseMerge(window, controller, workspaceId, otherId, paneId);
    }
    return terminalWorkspaceViewsReady(window, controller)
           && verifyTerminalPaneWindowInteractions(window, controller, outputDirectory);
}
} // namespace ztermy::ui
