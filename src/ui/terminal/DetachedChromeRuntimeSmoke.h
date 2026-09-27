#pragma once

#include <QJSValue>
#include "ui/terminal/DetachedPaneWindowRuntimeSmoke.h"

namespace ztermy::ui
{
template <typename Drag>
bool verifyDetachedInactiveTabMerge(NativeWindow &mainWindow, AppController &controller, QQuickWindow &target,
                                    const QString &targetId, const QString &otherId, Drag drag)
{
    using namespace std::chrono_literals;
    const auto originalMainGeometry = mainWindow.geometry();
    const auto originalTargetGeometry = target.geometry();
    const auto mainSelection = mainWindow.rootObject()->property("mainWorkspaceId");
    const auto originalPage = mainWindow.rootObject()->property("currentPage");
    const auto targetCount = controller.terminalWorkspace(targetId).value(QStringLiteral("paneCount")).toInt();
    const auto otherLayout = controller.terminalWorkspace(otherId).value(QStringLiteral("root"));
    const auto movingId = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    const auto paneId = controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
    const auto cleanup = qScopeGuard([&] {
        if (!controller.terminalWorkspace(movingId).isEmpty())
            controller.closeTerminalTab(movingId);
        else if (controller.activateTerminalPane(paneId))
            controller.closeActiveTerminalPane();
        mainWindow.rootObject()->setProperty("requestedMainWorkspaceId", mainSelection);
        QMetaObject::invokeMethod(mainWindow.rootObject(), "refreshMainWorkspace");
        mainWindow.rootObject()->setProperty("currentPage", originalPage);
        mainWindow.setGeometry(originalMainGeometry);
        target.setGeometry(originalTargetGeometry);
        processWindowEventsFor(300ms);
    });
    if (movingId.isEmpty())
        return false;
    mainWindow.rootObject()->setProperty("requestedMainWorkspaceId", movingId);
    mainWindow.rootObject()->setProperty("currentPage", QStringLiteral("terminal"));
    QMetaObject::invokeMethod(mainWindow.rootObject(), "refreshMainWorkspace");
    QMetaObject::invokeMethod(&target, "selectWorkspace", Q_ARG(QVariant, otherId));
    const auto area = target.screen()->availableGeometry();
    mainWindow.setGeometry({area.x() + 10, area.y() + 50, 500, 500});
    target.setGeometry({area.x() + 530, area.y() + 50, 480, 500});
    processWindowEventsFor(400ms);
    auto *handle =
        detachedVisualQuickItem(mainWindow.contentItem(), QStringLiteral("terminalPaneAction-headers-") + paneId);
    auto *tab = detachedVisualQuickItem(target.contentItem(), QStringLiteral("workspaceTitle-") + targetId);
    const bool dragged =
        handle && tab
        && drag(mainWindow, handle->mapToGlobal({14, 14}).toPoint(), tab->mapToGlobal({60, 16}).toPoint());
    qInfo() << "Detached merge fixture:" << bool(handle) << bool(tab) << dragged;
    processWindowEventsFor(400ms);
    const bool merged =
        dragged && controller.terminalWorkspace(movingId).isEmpty()
        && target.property("workspaceId").toString() == targetId
        && controller.terminalWorkspace(targetId).value(QStringLiteral("paneCount")).toInt() == targetCount + 1
        && controller.terminalWorkspace(otherId).value(QStringLiteral("root")) == otherLayout;
    qInfo() << "Pane drag to inactive detached Tab: switched, merged, other Tab preserved:" << merged;
    return merged;
}

inline bool verifyDetachedLogicalTabMerge(NativeWindow &mainWindow, AppController &controller, QQuickWindow &target,
                                          const QString &targetId, const QString &otherId)
{
    qInfo() << "Checking detached Tab drop geometry/transfer without desktop pointer injection";
    return verifyDetachedInactiveTabMerge(
        mainWindow, controller, target, targetId, otherId, [&](QQuickWindow &, const QPoint &, const QPoint &end) {
            auto *coordinator =
                mainWindow.rootObject()->findChild<QObject *>(QStringLiteral("terminalWindowCoordinator"));
            const auto sourceId = mainWindow.rootObject()->property("mainWorkspaceId").toString();
            const auto paneId = controller.terminalWorkspace(sourceId).value(QStringLiteral("activePaneId"));
            QVariant drop;
            qInfo() << "Detached coordinator found:" << bool(coordinator);
            if (!coordinator
                || !QMetaObject::invokeMethod(&target, "tabDropTarget", Q_RETURN_ARG(QVariant, drop),
                                              Q_ARG(QVariant, QPointF(end))))
                return false;
            if (drop.metaType() == QMetaType::fromType<QJSValue>())
                drop = drop.value<QJSValue>().toVariant();
            qInfo() << "Logical detached Tab target:" << drop;
            if (drop.toMap().value(QStringLiteral("mode")).toString() != QStringLiteral("merge"))
                return false;
            coordinator->setProperty("dropTarget", drop);
            return QMetaObject::invokeMethod(coordinator, "finishPaneDrop", Q_ARG(QVariant, paneId),
                                             Q_ARG(QVariant, false));
        });
}

inline bool verifyDetachedCreationMenu(NativeWindow &mainWindow, AppController &controller, QQuickWindow &window,
                                       const QString &outputDirectory)
{
    using namespace std::chrono_literals;
    auto *plus = detachedVisualQuickItem(window.contentItem(), QStringLiteral("detachedNewTerminalButton"));
    auto *reattach = detachedVisualQuickItem(window.contentItem(), QStringLiteral("detachedReattachAllButton"));
    if (!plus || !reattach || !qFuzzyCompare(plus->x() + plus->width(), reattach->x()))
        return false;
    if (!QMetaObject::invokeMethod(plus, "activated"))
        return false;
    processWindowEventsFor(200ms);
    auto *menu = window.findChild<QObject *>(QStringLiteral("detachedNewTerminalMenu"));
    const bool menuReady = menu && menu->property("visible").toBool() && !menu->property("mainWindowActions").toBool()
                           && menu->findChild<QObject *>(QStringLiteral("newTerminalShellMenu"))
                           && menu->findChild<QObject *>(QStringLiteral("newTerminalHostsMenu"));
    if (!menuReady)
        return false;
    const auto capture = window.contentItem()->grabToImage();
    if (!capture
        || !settleWindowUntil(
            [&] {
                return !capture->image().isNull();
            },
            3s)
        || !capture->image().save(QDir(outputDirectory).filePath(QStringLiteral("detached-new-menu.png"))))
        return false;
    const auto previous = window.property("workspaceId").toString();
    const auto mainSelection = mainWindow.rootObject()->property("mainWorkspaceId");
    const auto owner = window.property("ownerWindowId").toString();
    QMetaObject::invokeMethod(menu, "close");
    if (!QMetaObject::invokeMethod(menu, "localRequested", Q_ARG(QString, QStringLiteral("commandPrompt"))))
        return false;
    processWindowEventsFor(400ms);
    const auto created = controller.activeTerminalTabId();
    const bool placed = created != previous
                        && controller.terminalWorkspace(created).value(QStringLiteral("windowId")).toString() == owner
                        && mainWindow.rootObject()->property("mainWorkspaceId") == mainSelection;
    if (created != previous)
        controller.closeTerminalTab(created, previous);
    processWindowEventsFor(300ms);
    const auto count = controller.terminalTabs().size();
    // Missing credentials must prompt locally; this deliberately does not
    // connect to a server or read the user's saved hosts/credentials.
    const QVariantMap profile{{QStringLiteral("id"), QStringLiteral("prompt-only-fixture")},
                              {QStringLiteral("name"), QStringLiteral("Credential prompt test")},
                              {QStringLiteral("authentication"), QStringLiteral("password")},
                              {QStringLiteral("credentialRequired"), true},
                              {QStringLiteral("credentialStored"), false}};
    if (!QMetaObject::invokeMethod(menu, "hostRequested", Q_ARG(QVariant, profile)))
        return false;
    processWindowEventsFor(250ms);
    auto *dialog = window.findChild<QObject *>(QStringLiteral("savedHostCredentialDialog"));
    auto *field = detachedVisualQuickItem(window.contentItem(), QStringLiteral("savedCredentialField"));
    const bool localPrompt = dialog && dialog->property("visible").toBool() && field && field->window() == &window
                             && controller.terminalTabs().size() == count
                             && mainWindow.rootObject()->property("mainWorkspaceId") == mainSelection;
    if (dialog)
        QMetaObject::invokeMethod(dialog, "close");
    processWindowEventsFor(250ms);
    qInfo() << "Detached creation menu: shared entries, adjacent buttons, local ownership, local credential prompt:"
            << menuReady << placed << localPrompt;
    return menuReady && placed && localPrompt;
}
} // namespace ztermy::ui
