#pragma once

#include "application/AppController.h"
#include "platform/windows/NativeWindow.h"
#include "ui/WindowStateRuntimeSmoke.h"
#include "ui/terminal/DetachedMultiTabRuntimeSmoke.h"
#include "ui/terminal/DetachedPaneWindowRuntimeSmoke.h"
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
        const bool merged = controller.terminalWorkspace(workspaceId).isEmpty()
                            && controller.terminalWorkspace(otherId).value(QStringLiteral("paneCount")).toInt() == 3;
        qInfo() << "Tab drag merges all source panes into the target workspace:" << merged;
        passed = passed && merged;
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
    settle();
    auto *pane = window.findChild<TerminalItem *>(QStringLiteral("terminalViewport-") + paneId);
    bool passed = pane && qAbs(pane->y()) < 1;
    qInfo() << "Terminal uses full pane height without a title strip:" << passed;
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
    auto *header =
        visualQuickItem(root, (QStringLiteral("terminalPaneAction-headers-") + firstId).toLatin1().constData());
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
        qInfo() << "Pane drag handle click focuses pane:" << headerFocused;
        passed = passed && headerFocused;
        header =
            visualQuickItem(root, (QStringLiteral("terminalPaneAction-headers-") + secondId).toLatin1().constData());
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
        qInfo() << "Pane handle drag reorders existing sessions:" << reordered;
        passed = passed && reordered;
        controller.moveTerminalPane(firstId, secondId, QStringLiteral("horizontal"), false);
        controller.activateTerminalPane(paneId);
        settle();
    }
    else
        passed = false;
    QMetaObject::invokeMethod(root, "toggleTerminalPaneZoom", Q_ARG(QVariant, paneId), Q_ARG(QVariant, workspaceId));
    settle();
    passed = passed && root->property("zoomedTerminalPaneId").toString() == paneId;

    const QString otherId = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    settle();
    passed = passed && !otherId.isEmpty() && root->property("zoomedTerminalPaneId").toString().isEmpty();
    qInfo() << "Window transfer isolated zoom state:" << passed;
    settle();
    const QString singlePaneId = controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    auto *singleHandle =
        visualQuickItem(root, (QStringLiteral("terminalPaneAction-headers-") + singlePaneId).toLatin1().constData());
    if (singleHandle)
    {
        QPointF start = singleHandle->mapToScene({singleHandle->width() / 2, singleHandle->height() / 2});
        clickMouse(window, start);
        settle();
        auto *singleViewport = window.findChild<TerminalItem *>(QStringLiteral("terminalViewport-") + singlePaneId);
        const bool handleKeepsFullHeight = singleViewport && qAbs(singleViewport->y()) < 1;
        start = singleHandle->mapToScene({singleHandle->width() / 2, singleHandle->height() / 2});
        clickMouse(window, start);
        settle();
        const bool handleKeepsFocus =
            controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString() == singlePaneId;
        qInfo() << "Main pane handle retains full viewport and pane focus:" << handleKeepsFullHeight
                << handleKeepsFocus;
        passed = passed && handleKeepsFullHeight && handleKeepsFocus;
        start = singleHandle->mapToScene({singleHandle->width() / 2, singleHandle->height() / 2});
        const QPointF end{-30, start.y()};
        dragMouse(window, start, end, 8);
        settle();
        passed = passed && root->property("detachedTerminalPaneId").toString() == singlePaneId;
        qInfo() << "Hidden-title handle detaches a single pane:" << passed;
        QMetaObject::invokeMethod(root, "reattachTerminalPane");
        settle();
    }
    else
        passed = false;
    controller.activateTerminalTab(workspaceId);
    settle();
    passed = passed && root->property("zoomedTerminalPaneId").toString() == paneId;
    qInfo() << "Window transfer original zoom restored:" << passed;
    QMetaObject::invokeMethod(root, "toggleTerminalPaneZoom", Q_ARG(QVariant, paneId), Q_ARG(QVariant, workspaceId));
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
        const bool detachedHeaderHidden = !detached->property("tabBarVisible").toBool();
        qInfo() << "Detached tab bar is hidden by default:" << detachedHeaderHidden;
        passed = passed && detachedHeaderHidden;
        const auto handle = reinterpret_cast<HWND>(detached->winId()); // NOLINT(performance-no-int-to-ptr)
        auto *actions = detached->findChild<QQuickItem *>(QStringLiteral("terminalPaneActions-") + paneId);
        const bool toolbarHidden = actions && actions->opacity() < 0.01;
        QMetaObject::invokeMethod(detached, "toggleTabBar");
        settle();
        auto *maximizeAction = visualQuickItem(detached->contentItem(), "detachedWindowAction-maximize");
        bool nativeSnapHit = false;
        if (maximizeAction)
        {
            const QPointF center =
                maximizeAction->mapToScene({maximizeAction->width() / 2, maximizeAction->height() / 2});
            POINT screen{.x = qRound(center.x() * detached->devicePixelRatio()),
                         .y = qRound(center.y() * detached->devicePixelRatio())};
            ClientToScreen(handle, &screen);
            const LPARAM position = MAKELPARAM(screen.x, screen.y);
            nativeSnapHit = SendMessageW(handle, WM_NCHITTEST, 0, position) == HTMAXBUTTON;
        }
        const QPointF toolbarOrigin = actions ? actions->mapToScene(QPointF{}) : QPointF{-1, -1};
        sendMouse(*detached, toolbarOrigin + QPointF{12, 12}, Qt::NoButton, Qt::NoButton, QEvent::MouseMove);
        processWindowEventsFor(std::chrono::milliseconds{120});
        const bool toolbarRevealed = actions && actions->opacity() > 0.99;
        const auto *toolbarSurface = visualQuickItem(
            detached->contentItem(), (QStringLiteral("terminalPaneToolbarSurface-") + paneId).toLatin1().constData());
        const bool unifiedToolbarSurface = toolbarSurface && toolbarSurface->opacity() > 0.8;
        sendMouse(*detached, {40, 200}, Qt::NoButton, Qt::NoButton, QEvent::MouseMove);
        processWindowEventsFor(std::chrono::milliseconds{120});
        const bool toolbarHiddenAgain = actions && actions->opacity() < 0.01;
        qInfo() << "Detached pane toolbar reveals only on hover:" << toolbarHidden << toolbarRevealed
                << toolbarHiddenAgain << "surface=" << unifiedToolbarSurface << "snap=" << nativeSnapHit;
        passed =
            passed && toolbarHidden && toolbarRevealed && toolbarHiddenAgain && unifiedToolbarSurface && nativeSnapHit;
        const QPointF captionOrigin = maximizeAction ? maximizeAction->mapToScene(QPointF{}) : QPointF{-1, -1};
        const bool controlsFlush = maximizeAction && qAbs(captionOrigin.y()) < 1
                                   && qAbs(captionOrigin.x() + 2 * maximizeAction->width() - detached->width()) < 1;
        qInfo() << "Detached window controls align with top and right edges:" << controlsFlush;
        passed = passed && controlsFlush;
        passed = passed && GetWindow(handle, GW_OWNER) == nullptr
                 && (GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) == 0;
        const auto originalSize = detached->size();
        detached->resize(1100, 760);
        settle();
        passed =
            detached->grabWindow().save(QDir(outputDirectory).filePath(QStringLiteral("detached-pane-resized.png")))
            && passed;
        passed = verifyDetachedCaptionStateRoundTrip(*detached, outputDirectory) && passed;
        detached->resize(originalSize);
        controller.activateTerminalTab(otherId);
        settle();
        passed = passed && detached->isVisible()
                 && root->property("detachedTerminalWorkspaceId").toString() == detachedWorkspaceId;
    }
    passed = detached && verifyDetachedWindowReattach(window, controller, *detached, detachedWorkspaceId) && passed;
    passed = verifyNestedPaneEdges(window, controller, outputDirectory) && passed;
    passed = verifyDetachedCloseSelection(window, controller) && passed;
    qInfo() << "Pane handles, per-workspace zoom, detached taskbar/resize regression:" << passed;
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
    if (QCoreApplication::arguments().contains(QStringLiteral("--detached-tabs-only")))
        return verifyDetachedTabGrouping(window, controller, outputDirectory);
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
