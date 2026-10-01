#pragma once

#include "ui/terminal/DetachedPaneWindowRuntimeSmoke.h"

namespace ztermy::ui
{

inline bool verifyDetachedDropOcclusion(NativeWindow &mainWindow, QQuickWindow &detached, const QString &paneId)
{
    using namespace std::chrono_literals;
    auto *coordinator = mainWindow.rootObject()->findChild<QObject *>(QStringLiteral("terminalWindowCoordinator"));
    auto *view = detachedVisualQuickItem(detached.contentItem(), QStringLiteral("terminalViewport-") + paneId);
    if (!coordinator || !view)
        return false;
    windowing::present(detached);
    processWindowEventsFor(200ms);
    const auto point = view->mapToGlobal({view->width() * 0.15, view->height() * 0.5});
    const auto previousPointer = QCursor::pos();
    const auto restorePointer = qScopeGuard([&] {
        QCursor::setPos(previousPointer);
    });
    QCursor::setPos(point.toPoint());
    processWindowEventsFor(900ms);
    const auto resolve = [&] {
        QMetaObject::invokeMethod(coordinator, "updateDropTarget", Q_ARG(QVariant, QVariant::fromValue(point)));
        return coordinator->property("dropTarget").toMap();
    };
    const auto uncovered = resolve();
    qInfo() << "Detached uncovered probe:" << point << detached.geometry() << detached.isVisible()
            << detached.windowState() << "leaf=" << view->size() << view->mapToScene({0, 0}) << uncovered;
    const bool exactPane =
        uncovered.value(QStringLiteral("paneId")).toString() == paneId
        && uncovered.value(QStringLiteral("windowId")).toString() == detached.property("ownerWindowId").toString();
    QWindow blocker;
    blocker.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    blocker.setGeometry(detached.geometry());
    windowing::present(blocker);
    processWindowEventsFor(200ms);
    const bool obscured = resolve().isEmpty();
    const bool ignoresMovingSource =
        windowing::isUnobscuredDropTarget(detached, detached.mapFromGlobal(point.toPoint()), &blocker);
    blocker.hide();
    processWindowEventsFor(100ms);
    const bool visibleAgain = resolve().value(QStringLiteral("paneId")).toString() == paneId;
    coordinator->setProperty("dropTarget", QVariantMap{});
    qInfo() << "Detached drop: exact pane, obscured, ignore moving source, visible again:" << exactPane << obscured
            << ignoresMovingSource << visibleAgain;
    return exactPane && obscured && ignoresMovingSource && visibleAgain;
}

inline bool verifyDetachedSingleWorkspace(NativeWindow &window, AppController &controller,
                                          const QString &outputDirectory)
{
    using namespace std::chrono_literals;
    const auto id = controller.activeTerminalTabId();
    const auto pane = controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    if (!controller.detachTerminalWorkspace(id))
        return false;
    const auto owner = controller.terminalWorkspace(id).value(QStringLiteral("windowId")).toString();
    processWindowEventsFor(500ms);
    QPointer<QQuickWindow> detached;
    for (auto *candidate : QGuiApplication::allWindows())
        if (candidate->property("ownerWindowId").toString() == owner)
            detached = qobject_cast<QQuickWindow *>(candidate);
    if (!detached)
        return false;
    auto *view = detachedVisualQuickItem(detached->contentItem(), QStringLiteral("detachedWorkspaceViewport"));
    if (!view || view->y() != 0 || detached->property("windowControlsVisible").toBool()
        || detachedVisualQuickItem(detached->contentItem(), QStringLiteral("workspaceTitle-") + id))
        return false;
    const auto size = view->size();
    if (!QMetaObject::invokeMethod(detached, "toggleWindowControls"))
        return false;
    processWindowEventsFor(300ms);
    bool passed =
        view->size() == size && view->y() == 0 && verifyDetachedCaptionStateRoundTrip(*detached, outputDirectory);
    const auto tabCount = controller.terminalTabs().size();
    const int paneCount = controller.terminalWorkspace(id).value(QStringLiteral("paneCount")).toInt();
    const bool created = QMetaObject::invokeMethod(detached, "openLocalTab", Q_ARG(QVariant, QString{}),
                                                   Q_ARG(QVariant, QStringLiteral("commandPrompt")));
    processWindowEventsFor(400ms);
    passed = created && controller.terminalTabs().size() == tabCount
             && controller.terminalWorkspace(id).value(QStringLiteral("paneCount")).toInt() == paneCount + 1 && passed;
    const auto createdPane = controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    passed = controller.activateTerminalPane(createdPane) && controller.closeActiveTerminalPane() && passed;
    processWindowEventsFor(300ms);
    // Keep this owned target away from the host app's topmost side panel.
    // It must be exposed before testing whether another window blocks drops.
    detached->setGeometry(100, 100, 940, 660);
    detached->hide();
    detached->show();
    windowing::present(*detached);
    processWindowEventsFor(300ms);
    const auto detachedHandle = reinterpret_cast<HWND>(detached->winId()); // NOLINT(performance-no-int-to-ptr)
    // Background launches cannot steal foreground activation. Expose only this
    // owned fixture above unrelated desktop windows; the blocker below is also
    // topmost so the occlusion rejection remains an actual negative check.
    const auto restoreZOrder = qScopeGuard([&] {
        if (detached)
            SetWindowPos(detachedHandle, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    });
    SetWindowPos(detachedHandle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    passed = verifyDetachedDropOcclusion(window, *detached, pane) && passed;
    const auto incoming = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    const auto incomingPane = controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    auto *coordinator = window.rootObject()->findChild<QObject *>(QStringLiteral("terminalWindowCoordinator"));
    auto *leaf = detachedVisualQuickItem(detached->contentItem(), QStringLiteral("terminalViewport-") + pane);
    if (!coordinator || !leaf || incoming.isEmpty())
        return false;
    windowing::present(*detached);
    processWindowEventsFor(300ms);
    const auto point = leaf->mapToGlobal({leaf->width() * 0.1, leaf->height() * 0.5});
    QMetaObject::invokeMethod(coordinator, "updateDropTarget", Q_ARG(QVariant, QVariant::fromValue(point)));
    const auto target = coordinator->property("dropTarget").toMap();
    passed = target.value(QStringLiteral("mode")).toString() == QStringLiteral("merge")
             && target.value(QStringLiteral("paneId")).toString() == pane && passed;
    QMetaObject::invokeMethod(coordinator, "finishTabDrop", Q_ARG(QVariant, incoming), Q_ARG(QVariant, false));
    processWindowEventsFor(500ms);
    passed = controller.terminalWorkspace(incoming).isEmpty()
             && controller.terminalWorkspace(id).value(QStringLiteral("paneCount")).toInt() == paneCount + 1
             && detached->property("tabs").toList().size() == 1 && passed;
    qInfo() << "Detached: no Tabs, overlay controls, new Pane, incoming merge:" << passed;
    if (!controller.activateTerminalPane(incomingPane) || !controller.closeActiveTerminalPane())
        return false;
    processWindowEventsFor(250ms);
    detached->setProperty("windowControlsVisible", true);
    passed =
        detached->grabWindow().save(QDir(outputDirectory).filePath(QStringLiteral("detached-single-workspace.png")))
        && verifyDetachedWindowReattach(window, controller, *detached, id) && passed;
    qInfo() << "Detached single workspace lifecycle passed=" << passed;
    return passed;
}
} // namespace ztermy::ui
