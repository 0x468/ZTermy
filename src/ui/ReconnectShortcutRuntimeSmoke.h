#pragma once

#include "application/AppController.h"
#include "ui/RuntimeSmokeItems.h"
#include "ui/terminal/TerminalItem.h"

#include <QClipboard>
#include <QDir>
#include <QHostAddress>
#include <QMimeData>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>

namespace ztermy::ui
{
inline bool verifyReconnectShortcutRuntime(NativeWindow &window, AppController &controller)
{
    using namespace std::chrono_literals;
    qInfo() << "Reconnect shortcut runtime started";
    // A loopback peer closes before any authentication: no remote account,
    // key, credential store or Shell command is involved in this gate.
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost))
        return false;
    int connections = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (auto *socket = server.nextPendingConnection())
        {
            ++connections;
            socket->abort();
            socket->deleteLater();
        }
    });
    const auto name = QStringLiteral("Reconnect shortcut runtime fixture");
    if (!controller.saveHostProfile({}, name, QStringLiteral("127.0.0.1"), server.serverPort(),
                                    QStringLiteral("fixture"), QStringLiteral("private-key"),
                                    QStringLiteral("unused-test-key"), false, {}))
        return false;
    QString profileId;
    for (const auto &entry : controller.hostProfiles())
        if (entry.toMap().value(QStringLiteral("name")) == name)
            profileId = entry.toMap().value(QStringLiteral("id")).toString();
    QString workspaceId;
    const auto cleanup = qScopeGuard([&] {
        if (!workspaceId.isEmpty())
            (void)controller.closeTerminalTab(workspaceId);
        (void)controller.deleteHostProfile(profileId);
        (void)controller.resetActionShortcut(QStringLiteral("terminal.reconnect"));
    });
    if (profileId.isEmpty() || !controller.connectHostProfile(profileId, {}))
        return false;
    workspaceId = controller.activeTerminalTabId();
    const auto ready = [&] {
        return controller.terminalWorkspace(workspaceId)
            .value(QStringLiteral("root"))
            .toMap()
            .value(QStringLiteral("tab"))
            .toMap()
            .value(QStringLiteral("canReconnect"))
            .toBool();
    };
    if (!processWindowEventsUntil(ready, 5s) || connections != 1)
    {
        qWarning() << "Reconnect shortcut initial loopback failure unavailable" << connections << ready();
        return false;
    }
    qInfo() << "Reconnect shortcut initial failure ready";
    window.rootObject()->setProperty("currentPage", QStringLiteral("terminal"));
    window.raise();
    window.requestActivate();
    SetForegroundWindow(reinterpret_cast<HWND>(window.winId())); // NOLINT(performance-no-int-to-ptr)
    const auto verifyWindow = [&](QQuickWindow &target) {
        const auto paneId = controller.terminalWorkspace(workspaceId).value(QStringLiteral("activePaneId")).toString();
        auto *viewport = qobject_cast<TerminalItem *>(
            quickItem(target.contentItem(), (QStringLiteral("terminalViewport-") + paneId).toLatin1().constData()));
        if (!viewport
            || !processWindowEventsUntil(
                [&] {
                    return target.isActive();
                },
                2s))
            return false;
        viewport->forceActiveFocus();
        processWindowEventsFor(200ms);
        const auto tab = controller.terminalWorkspace(workspaceId)
                             .value(QStringLiteral("root"))
                             .toMap()
                             .value(QStringLiteral("tab"))
                             .toMap();
        auto *strip = quickItem(target.contentItem(), (QStringLiteral("terminalSessionStateStrip-")
                                                       + tab.value(QStringLiteral("sessionId")).toString())
                                                          .toLatin1()
                                                          .constData());
        if (!strip)
            return false;
        auto *dismiss = strip->findChild<QQuickItem *>(QStringLiteral("dismissSessionStatus-")
                                                       + tab.value(QStringLiteral("sessionId")).toString());
        if (!dismiss || !QMetaObject::invokeMethod(dismiss, "click"))
            return false;
        if (!processWindowEventsUntil(
                [&] {
                    return strip->height() == 0;
                },
                1s))
            return false;
        const auto press = [&](int key, Qt::KeyboardModifiers modifiers) {
            qt_handleKeyEvent(&target, QEvent::KeyPress, key, modifiers);
            qt_handleKeyEvent(&target, QEvent::KeyRelease, key, modifiers);
            processWindowEventsFor(100ms);
        };
        int before = connections;
        auto *search =
            quickItem(target.contentItem(), &target == &window ? "mainTerminalSearch" : "detachedTerminalSearch");
        if (!search || !QMetaObject::invokeMethod(search, "openSearch"))
            return false;
        processWindowEventsFor(100ms);
        if (viewport->hasActiveFocus())
        {
            qWarning() << "Reconnect shortcut search did not receive focus";
            return false;
        }
        press(Qt::Key_R, Qt::ControlModifier);
        if (connections != before)
        {
            qWarning() << "Reconnect shortcut consumed a search field key";
            return false;
        }
        (void)QMetaObject::invokeMethod(search, "closeSearch");
        qInfo() << "Reconnect shortcut input field guard passed";
        viewport->forceActiveFocus();
        press(Qt::Key_R, Qt::ControlModifier);
        if (!processWindowEventsUntil(
                [&] {
                    return connections == before + 1 && ready();
                },
                5s))
        {
            qWarning() << "Reconnect shortcut default dispatch failed" << connections << before << ready();
            return false;
        }
        qInfo() << "Reconnect shortcut default dispatch passed";
        if (!controller.setActionShortcut(QStringLiteral("terminal.reconnect"), QStringLiteral("Ctrl+Alt+R"))
                 .value(QStringLiteral("valid"))
                 .toBool())
            return false;
        before = connections;
        viewport->forceActiveFocus();
        press(Qt::Key_R, Qt::ControlModifier);
        if (connections != before)
        {
            qWarning() << "Reconnect shortcut previous binding stayed enabled";
            return false;
        }
        press(Qt::Key_R, Qt::ControlModifier | Qt::AltModifier);
        if (!processWindowEventsUntil(
                [&] {
                    return connections == before + 1 && ready();
                },
                5s))
        {
            qWarning() << "Reconnect shortcut changed binding did not dispatch" << connections << before << ready();
            return false;
        }
        qInfo() << "Reconnect shortcut rebind passed";
        if (!controller.setActionShortcut(QStringLiteral("terminal.reconnect"), {})
                 .value(QStringLiteral("valid"))
                 .toBool())
            return false;
        before = connections;
        viewport->forceActiveFocus();
        press(Qt::Key_R, Qt::ControlModifier);
        press(Qt::Key_R, Qt::ControlModifier | Qt::AltModifier);
        if (connections != before || !controller.resetActionShortcut(QStringLiteral("terminal.reconnect")))
            return false;
        qInfo() << "Reconnect shortcut unbind/reset passed";
        if (!controller.saveSessionLifecycleSettings(false, true, true, false, true))
            return false;
        before = connections;
        viewport->forceActiveFocus();
        press(Qt::Key_R, Qt::ControlModifier);
        if (!processWindowEventsUntil(
                [&] {
                    return connections == before + 1 && ready();
                },
                5s))
            return false;
        const QString marker =
            QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "--- New SSH connection ---");
        controller.searchTerminal(marker, false, true);
        if (!processWindowEventsUntil(
                [&] {
                    return controller.terminalSearchTotal() == 1;
                },
                3s)
            || !captureWindowSmokeItem(target.contentItem(), &target == &window
                                                                 ? QStringLiteral("reconnect-history-main.png")
                                                                 : QStringLiteral("reconnect-history-detached.png")))
            return false;
        controller.clearTerminalSearch();
        if (!controller.saveSessionLifecycleSettings(false, true, true, false, false))
            return false;
        before = connections;
        viewport->forceActiveFocus();
        press(Qt::Key_R, Qt::ControlModifier);
        if (!processWindowEventsUntil(
                [&] {
                    return connections == before + 1 && ready();
                },
                5s))
            return false;
        controller.searchTerminal(marker, false, true);
        processWindowEventsFor(200ms);
        if (controller.terminalSearchTotal() != 0)
            return false;
        controller.clearTerminalSearch();
        qInfo() << "Reconnect history opt-in/out native routing passed";
        return captureWindowSmokeItem(target.contentItem(), &target == &window
                                                                ? QStringLiteral("reconnect-shortcut-main.png")
                                                                : QStringLiteral("reconnect-shortcut-detached.png"));
    };
    if (!verifyWindow(window) || !controller.detachTerminalWorkspace(workspaceId))
        return false;
    qInfo() << "Reconnect shortcut main routing passed";
    processWindowEventsFor(400ms);
    const auto owner = controller.terminalWorkspace(workspaceId).value(QStringLiteral("windowId")).toString();
    for (auto *candidate : QGuiApplication::allWindows())
        if (candidate->property("ownerWindowId").toString() == owner)
        {
            auto *detached = qobject_cast<QQuickWindow *>(candidate);
            if (!detached)
                return false;
            detached->raise();
            detached->requestActivate();
            SetForegroundWindow(reinterpret_cast<HWND>(detached->winId())); // NOLINT(performance-no-int-to-ptr)
            return verifyWindow(*detached);
        }
    return false;
}

} // namespace ztermy::ui
