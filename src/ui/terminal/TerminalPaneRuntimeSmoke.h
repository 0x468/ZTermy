#pragma once

#include "application/AppController.h"
#include "platform/windows/NativeWindow.h"
#include "ui/terminal/TerminalItem.h"

#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQuickItem>
#include <QTimer>
#include <vector>

QT_BEGIN_NAMESPACE
Q_GUI_EXPORT void qt_handleMouseEvent(QWindow *window, const QPointF &local, const QPointF &global,
                                      Qt::MouseButtons state, Qt::MouseButton button, QEvent::Type type,
                                      Qt::KeyboardModifiers modifiers, int timestamp);
QT_END_NAMESPACE

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
inline void processWindowEventsFor(const std::chrono::milliseconds duration)
{
    QEventLoop loop;
    QTimer::singleShot(duration, &loop, &QEventLoop::quit);
    loop.exec();
}

// Synthesizes one mouse event in window scene coordinates and lets the scene react.
inline void sendMouse(QQuickWindow &window, const QPointF &point, const Qt::MouseButtons buttons,
                      const Qt::MouseButton button, const QEvent::Type type,
                      const std::chrono::milliseconds settle = std::chrono::milliseconds{30})
{
    qt_handleMouseEvent(&window, point, window.mapToGlobal(point.toPoint()), buttons, button, type, Qt::NoModifier,
                        static_cast<int>(GetTickCount()));
    processWindowEventsFor(settle);
}

// A real pointer hovers before it presses. The pane drag capture layer in
// Main.qml only enables itself while a header drag area is hovered, so a
// synthetic press without the preceding move never reaches it.
inline void clickMouse(QQuickWindow &window, const QPointF &point)
{
    sendMouse(window, point, Qt::NoButton, Qt::NoButton, QEvent::MouseMove);
    sendMouse(window, point, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
    sendMouse(window, point, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
}

inline void dragMouse(QQuickWindow &window, const QPointF &start, const QPointF &end, const int steps,
                      const std::chrono::milliseconds settle = std::chrono::milliseconds{30})
{
    sendMouse(window, start, Qt::NoButton, Qt::NoButton, QEvent::MouseMove, settle);
    sendMouse(window, start, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress, settle);
    for (int step = 1; step <= steps; ++step)
        sendMouse(window, start + (end - start) * (static_cast<double>(step) / steps), Qt::LeftButton, Qt::NoButton,
                  QEvent::MouseMove, settle);
    sendMouse(window, end, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease, settle);
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
        detached->showMaximized();
        settle();
        passed =
            detached->grabWindow().save(QDir(outputDirectory).filePath(QStringLiteral("detached-pane-maximized.png")))
            && passed;
        detached->showNormal();
        detached->resize(originalSize);
        controller.activateTerminalTab(otherId);
        settle();
        passed = passed && detached->isVisible()
                 && root->property("detachedTerminalWorkspaceId").toString() == detachedWorkspaceId;
    }
    QMetaObject::invokeMethod(root, "reattachTerminalPane");
    qInfo() << "Window transfer native material and reattachment:" << passed;
    passed = verifyWholeTabMouseMerge(window, controller, workspaceId, otherId, paneId) && passed;
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
