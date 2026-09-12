#pragma once

#include "application/AppController.h"
#include "platform/windows/NativeWindow.h"
#include "ui/terminal/TerminalItem.h"

#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQuickItem>
#include <QTimer>

QT_BEGIN_NAMESPACE
Q_GUI_EXPORT void qt_handleMouseEvent(QWindow *window, const QPointF &local, const QPointF &global,
                                      Qt::MouseButtons state, Qt::MouseButton button, QEvent::Type type,
                                      Qt::KeyboardModifiers modifiers, int timestamp);
QT_END_NAMESPACE

namespace ztermy::ui
{
inline void processWindowEventsFor(const std::chrono::milliseconds duration)
{
    QEventLoop loop;
    QTimer::singleShot(duration, &loop, &QEventLoop::quit);
    loop.exec();
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
    const auto send = [&window](const QPointF &point, Qt::MouseButtons buttons, Qt::MouseButton button,
                                QEvent::Type type) {
        qt_handleMouseEvent(&window, point, window.mapToGlobal(point.toPoint()), buttons, button, type, Qt::NoModifier,
                            static_cast<int>(GetTickCount()));
        processWindowEventsFor(std::chrono::milliseconds{30});
    };
    send(start, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
    send(end, Qt::LeftButton, Qt::NoButton, QEvent::MouseMove);
    const qreal live = root->property("activeTerminalWorkbenchWidth").toReal();
    const bool liveResize =
        root->property("workbenchResizeInProgress").toBool() && qAbs(live - original - 60) < 2
        && qAbs(viewport->x() - live) < 2
        && qAbs(controller.terminalTabs().constFirst().toMap().value(QStringLiteral("workbenchWidth")).toReal()
                - original)
               < 1;
    send(end, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
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
    const auto layout = controller.activeTerminalWorkspace().value(QStringLiteral("root")).toMap();
    const QString firstId = layout.value(QStringLiteral("first")).toMap().value(QStringLiteral("id")).toString();
    const QString secondId = layout.value(QStringLiteral("second")).toMap().value(QStringLiteral("id")).toString();
    auto *header = window.findChild<QQuickItem *>(QStringLiteral("terminalPaneHeader-") + firstId);
    auto *targetPane = window.findChild<TerminalItem *>(QStringLiteral("terminalViewport-") + secondId);
    if (header && targetPane)
    {
        const auto send = [&window](const QPointF &point, Qt::MouseButtons buttons, Qt::MouseButton button,
                                    QEvent::Type type) {
            qt_handleMouseEvent(&window, point, window.mapToGlobal(point.toPoint()), buttons, button, type,
                                Qt::NoModifier, static_cast<int>(GetTickCount()));
            processWindowEventsFor(std::chrono::milliseconds{30});
        };
        const QPointF clickPoint = header->mapToScene(QPointF(12, 16));
        send(clickPoint, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
        send(clickPoint, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
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
        const QPointF end = targetPane->mapToScene(QPointF(targetPane->width() / 2, -16));
        send(start, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
        for (int step = 1; step <= 8; ++step)
            send(start + (end - start) * (static_cast<double>(step) / 8.0), Qt::LeftButton, Qt::NoButton,
                 QEvent::MouseMove);
        send(end, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
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
    QMetaObject::invokeMethod(root, "toggleTerminalPaneHeaders");
    settle();
    const QString singlePaneId = controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    auto *singleHeader = window.findChild<QQuickItem *>(QStringLiteral("terminalPaneHeader-") + singlePaneId);
    if (singleHeader)
    {
        const QPointF start = singleHeader->mapToScene({12, 16});
        const QPointF end{-30, start.y()};
        const auto send = [&window](const QPointF &point, Qt::MouseButtons buttons, Qt::MouseButton button,
                                    QEvent::Type type) {
            qt_handleMouseEvent(&window, point, window.mapToGlobal(point.toPoint()), buttons, button, type,
                                Qt::NoModifier, static_cast<int>(GetTickCount()));
            processWindowEventsFor(std::chrono::milliseconds{30});
        };
        send(start, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
        for (int step = 1; step <= 8; ++step)
            send(start + (end - start) * (static_cast<double>(step) / 8.0), Qt::LeftButton, Qt::NoButton,
                 QEvent::MouseMove);
        send(end, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
        settle();
        passed = passed && root->property("detachedTerminalPaneId").toString() == singlePaneId;
        QMetaObject::invokeMethod(root, "reattachTerminalPane");
        settle();
    }
    else
        passed = false;
    controller.activateTerminalTab(workspaceId);
    settle();
    passed = passed && root->property("zoomedTerminalPaneId").toString() == paneId;
    QMetaObject::invokeMethod(root, "toggleTerminalPaneZoom", Q_ARG(QVariant, paneId));
    QMetaObject::invokeMethod(root, "detachTerminalPane", Q_ARG(QVariant, paneId));
    settle();

    QQuickWindow *detached = nullptr;
    for (auto *candidate : QGuiApplication::allWindows())
        if (candidate->objectName() == QStringLiteral("detachedTerminalWindow"))
            detached = qobject_cast<QQuickWindow *>(candidate);
    passed = passed && detached && detached->isVisible() && !detached->transientParent();
    if (detached)
    {
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
        passed =
            passed && detached->isVisible() && root->property("detachedTerminalWorkspaceId").toString() == workspaceId;
    }
    QMetaObject::invokeMethod(root, "reattachTerminalPane");
    controller.closeTerminalTab(otherId);
    controller.activateTerminalTab(workspaceId);
    settle();
    qInfo() << "Pane headers, per-workspace zoom, detached taskbar/resize regression:" << passed;
    return passed;
}
} // namespace ztermy::ui
