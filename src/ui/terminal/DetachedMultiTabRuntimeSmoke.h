#pragma once

#include <QGuiApplication>
#include <QQuickItemGrabResult>
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
    const auto resolve = [&] {
        QMetaObject::invokeMethod(coordinator, "updateDropTarget", Q_ARG(QVariant, QVariant::fromValue(point)));
        return coordinator->property("dropTarget").toMap();
    };
    const auto uncovered = resolve();
    const bool exactPane =
        uncovered.value(QStringLiteral("paneId")).toString() == paneId
        && uncovered.value(QStringLiteral("windowId")).toString() == detached.property("ownerWindowId").toString();
    QWindow blocker;
    blocker.setFlags(Qt::Tool | Qt::FramelessWindowHint);
    blocker.setGeometry(detached.geometry());
    windowing::present(blocker);
    processWindowEventsFor(200ms);
    const bool obscured = resolve().isEmpty();
    coordinator->setProperty("movingWindow", QVariant::fromValue(static_cast<QObject *>(&blocker)));
    const bool ignoresMovingSource = resolve().value(QStringLiteral("paneId")).toString() == paneId;
    coordinator->setProperty("movingWindow", QVariant::fromValue(static_cast<QObject *>(nullptr)));
    blocker.hide();
    processWindowEventsFor(100ms);
    const bool visibleAgain = resolve().value(QStringLiteral("paneId")).toString() == paneId;
    coordinator->setProperty("dropTarget", QVariantMap{});
    qInfo() << "Detached drop: exact pane, obscured, ignore moving source, visible again:" << exactPane << obscured
            << ignoresMovingSource << visibleAgain;
    return exactPane && obscured && ignoresMovingSource && visibleAgain;
}

inline bool verifyDetachedTabGrouping(NativeWindow &window, AppController &controller, const QString &outputDirectory)
{
    using namespace std::chrono_literals;
    const auto settle = [&window] {
        window.requestUpdate();
        processWindowEventsFor(400ms);
    };
    const auto a = controller.activeTerminalTabId();
    const auto pane = controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    if (!controller.detachTerminalWorkspace(a))
        return false;
    settle();
    const auto owner = controller.terminalWorkspace(a).value(QStringLiteral("windowId")).toString();
    const auto b = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    const auto main = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    if (b.isEmpty() || main.isEmpty() || !controller.insertTerminalWorkspace(b, 1, owner))
        return false;
    settle();
    QPointer<QQuickWindow> detached;
    int count = 0;
    for (auto *candidate : QGuiApplication::allWindows())
        if (candidate->objectName() == QStringLiteral("detachedTerminalWindow") && candidate->isVisible())
        {
            ++count;
            detached = qobject_cast<QQuickWindow *>(candidate);
        }
    if (count != 1 || !detached || detached->property("workspaceId").toString() != b)
        return false;
    auto *tab = detachedVisualQuickItem(detached->contentItem(), QStringLiteral("workspaceTitle-") + a);
    if (!tab || !QMetaObject::invokeMethod(tab, "activated"))
        return false;
    settle();
    const bool selected =
        detached->property("workspaceId").toString() == a
        && detachedVisualQuickItem(detached->contentItem(), QStringLiteral("terminalViewport-") + pane);
    controller.activateTerminalTab(main);
    settle();
    const bool independent = detached->property("workspaceId").toString() == a;
    const bool dropOcclusion = verifyDetachedDropOcclusion(window, *detached, pane);
    tab = detachedVisualQuickItem(detached->contentItem(), QStringLiteral("workspaceTitle-") + a);
    if (!tab || !QMetaObject::invokeMethod(tab, "renameRequested"))
        return false;
    settle();
    auto *dialog = detached->findChild<QObject *>(QStringLiteral("terminalRenameDialog"));
    auto *field = detachedVisualQuickItem(detached->contentItem(), QStringLiteral("renameTerminalTitleField"));
    const bool localDialog = dialog && dialog->property("visible").toBool() && field && field->window() == detached;
    if (!localDialog)
        return false;
    field->setProperty("text", QStringLiteral("Detached renamed"));
    QMetaObject::invokeMethod(field, "accepted");
    settle();
    const bool renamed = !dialog->property("visible").toBool()
                         && controller.terminalWorkspace(a).value(QStringLiteral("title")).toString()
                                == QStringLiteral("Detached renamed");
    tab = detachedVisualQuickItem(detached->contentItem(), QStringLiteral("workspaceTitle-") + a);
    if (!tab || !QMetaObject::invokeMethod(tab, "duplicateRequested"))
        return false;
    settle();
    const auto duplicate = controller.activeTerminalTabId();
    const bool duplicated =
        duplicate != a && duplicate != b && duplicate != main
        && controller.terminalWorkspace(duplicate).value(QStringLiteral("windowId")).toString() == owner;
    if (!duplicated)
        return false;
    QMetaObject::invokeMethod(detached, "closeTab", Q_ARG(QVariant, duplicate));
    QMetaObject::invokeMethod(detached, "selectWorkspace", Q_ARG(QVariant, a));
    settle();
    const bool captionRoundTrip = verifyDetachedCaptionStateRoundTrip(*detached, outputDirectory);
    settle();
    const auto capture = detached->contentItem()->grabToImage();
    const bool captured =
        capture
        && settleWindowUntil(
            [&] {
                return !capture->image().isNull();
            },
            3s)
        && capture->image().save(QDir(outputDirectory).filePath(QStringLiteral("detached-multi-tab.png")));
    QMetaObject::invokeMethod(detached, "closeTab", Q_ARG(QVariant, a));
    settle();
    const bool keptWindow = detached && detached->isVisible() && detached->property("workspaceId").toString() == b
                            && controller.terminalWorkspace(a).isEmpty();
    if (detached)
        detached->close();
    settle();
    const bool closed =
        detached.isNull() && controller.terminalWorkspace(b).isEmpty() && !controller.terminalWorkspace(main).isEmpty();
    qInfo() << "Detached multi-tab: selected, independent, captured, same window after close, closed group:" << selected
            << independent << captured << keptWindow << closed;
    qInfo() << "Detached local rename and duplicate:" << localDialog << renamed << duplicated;
    return selected && independent && captured && keptWindow && closed && localDialog && renamed && duplicated
           && dropOcclusion && captionRoundTrip;
}
} // namespace ztermy::ui
