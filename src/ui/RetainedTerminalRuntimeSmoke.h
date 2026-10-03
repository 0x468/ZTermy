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

inline bool verifyRetainedTerminal(NativeWindow &window, AppController &controller)
{
    using namespace std::chrono_literals;
    const auto arguments = QCoreApplication::arguments();
    const auto data = arguments.indexOf(QStringLiteral("--data-dir"));
    const QDir expected(
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("test-data/retained-terminal-runtime")));
    if (qEnvironmentVariable("ZTERMY_TEST_ISOLATED_SHELLS") != QStringLiteral("1") || data < 0
        || data + 1 >= arguments.size() || expected.canonicalPath().isEmpty()
        || QDir(arguments[data + 1]).canonicalPath() != expected.canonicalPath() || !controller.terminalTabs().isEmpty()
        || controller.closePaneOnSessionEnd())
        return false;
    if (!verifyReconnectShortcutRuntime(window, controller))
        return false;
    qInfo() << "Reconnect shortcut main/detached routing passed";
    auto clipboardBackup = std::make_unique<QMimeData>();
    if (const auto *mime = QGuiApplication::clipboard()->mimeData())
        for (const auto &format : mime->formats())
            clipboardBackup->setData(format, mime->data(format));
    const auto id = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    if (id.isEmpty())
        return false;
    const auto originalTheme = controller.terminalThemeId();
    const auto cleanup = qScopeGuard([&] {
        (void)controller.closeTerminalTab(id);
        controller.saveTerminalTheme(originalTheme);
        window.rootObject()->setProperty("appearancePreviewActive", false);
        auto *clipboard = QGuiApplication::clipboard();
        if (clipboard->ownsClipboard())
            clipboard->clear();
        clipboard->setMimeData(clipboardBackup.release());
    });
    window.rootObject()->setProperty("currentPage", QStringLiteral("terminal"));
    QPointer<TerminalItem> viewport;
    if (!processWindowEventsUntil(
            [&] {
                const auto workspace = controller.terminalWorkspace(id);
                const auto pane = workspace.value(QStringLiteral("activePaneId")).toString();
                viewport = qobject_cast<TerminalItem *>(quickItem(
                    window.rootObject(), (QStringLiteral("terminalViewport-") + pane).toLatin1().constData()));
                return viewport && !viewport->readOnly();
            },
            5s))
        return false;
    int generatedKeys = 0;
    const auto keyConnection = QObject::connect(viewport, &TerminalItem::keyEventGenerated, viewport, [&] {
        ++generatedKeys;
    });
    viewport->forceActiveFocus();
    sendKey(window, Qt::Key_R, Qt::ControlModifier);
    QObject::disconnect(keyConnection);
    if (generatedKeys == 0)
    {
        qWarning() << "Live Shell Ctrl+R was intercepted";
        return false;
    }
    // CMD does not implement Shell history search; clear any control-key
    // line-editing effect before sending the separate output fixture.
    viewport->inputGenerated(QByteArrayLiteral("\x03"));
    processWindowEventsFor(100ms);
    viewport->inputGenerated(QByteArrayLiteral("for /L %i in (1,1,60) do @echo ZTERMY_RETAINED_%i\r"));
    processWindowEventsFor(500ms);
    viewport->inputGenerated(QByteArrayLiteral("exit\r"));
    if (!processWindowEventsUntil(
            [&] {
                return viewport && viewport->readOnly();
            },
            5s))
        return false;
    processWindowEventsFor(300ms);
    const auto sessionId = controller.activeTerminalWorkspace()
                               .value(QStringLiteral("root"))
                               .toMap()
                               .value(QStringLiteral("tab"))
                               .toMap()
                               .value(QStringLiteral("sessionId"))
                               .toString();
    auto *strip = quickItem(window.rootObject(),
                            (QStringLiteral("terminalSessionStateStrip-") + sessionId).toLatin1().constData());
    if (!strip || strip->height() != 44 || viewport->y() != 0
        || viewport->mapToScene({0, viewport->height()}).y() > strip->mapToScene({0, 0}).y() + 1)
    {
        qWarning() << "Retained terminal geometry check" << "stripFound=" << (strip != nullptr)
                   << "stripHeight=" << (strip ? strip->height() : -1) << "viewportY=" << viewport->y();
        (void)captureWindowSmokeItem(window.contentItem(), QStringLiteral("retained-terminal-geometry-failed.png"));
        return false;
    }
    viewport->selectAllTerminal();
    if (!processWindowEventsUntil(
            [&] {
                return viewport->hasSelection();
            },
            3s))
        return false;
    viewport->forceActiveFocus();
    sendKey(window, Qt::Key_Insert, Qt::ControlModifier);
    if (!processWindowEventsUntil(
            [] {
                const auto text = QGuiApplication::clipboard()->text();
                return text.contains(QStringLiteral("ZTERMY_RETAINED_1"))
                       && text.contains(QStringLiteral("ZTERMY_RETAINED_60"));
            },
            3s))
        return false;
    // Establish that repeated writes work before transferring the Pane.
    viewport->setClipboardText(QStringLiteral("ZTERMY_MAIN_COPY_PENDING"));
    if (QGuiApplication::clipboard()->text() != QStringLiteral("ZTERMY_MAIN_COPY_PENDING"))
    {
        qWarning() << "Retained main repeated clipboard write failed";
        return false;
    }
    sendKey(window, Qt::Key_Insert, Qt::ControlModifier);
    controller.searchTerminal(QStringLiteral("ZTERMY_RETAINED_60"), false, true);
    if (!processWindowEventsUntil(
            [&] {
                return controller.terminalSearchTotal() > 0;
            },
            3s))
        return false;
    viewport->scrollLines(-20);
    processWindowEventsFor(100ms);
    if (!captureWindowSmokeItem(window.contentItem(), QStringLiteral("retained-terminal-readable.png")))
        return false;
    auto *dismiss = strip->findChild<QQuickItem *>(QStringLiteral("dismissSessionStatus-") + sessionId);
    if (!dismiss)
        return false;
    sendMouseClick(window, *dismiss, {dismiss->width() / 2, dismiss->height() / 2});
    if (!processWindowEventsUntil(
            [&] {
                return strip->height() == 0;
            },
            1s))
        return false;
    if (!viewport->readOnly()
        || !captureWindowSmokeItem(window.contentItem(), QStringLiteral("retained-terminal-dismissed.png")))
        return false;

    // Observe actual intermediate geometry, not merely the final hidden frame.
    auto *context = QQmlEngine::contextForObject(window.rootObject());
    auto *motion = context ? context->engine()->singletonInstance<QObject *>("Ztermy", "Motion") : nullptr;
    auto *shield = strip->findChild<QQuickItem *>(QStringLiteral("sessionStateInputShield"));
    if (!motion || !shield)
    {
        qWarning() << "Retained status motion objects" << (motion != nullptr) << (shield != nullptr);
        return false;
    }
    for (const auto *effects : {"full", "reduced", "off"})
    {
        window.rootObject()->setProperty("previewEffectsTier", QString::fromLatin1(effects));
        window.rootObject()->setProperty("appearancePreviewActive", true);
        processWindowEventsFor(30ms);
        strip->setProperty("dismissed", false);
        if (!processWindowEventsUntil(
                [&] {
                    return strip->height() == 44;
                },
                1s))
        {
            qWarning() << "Retained status did not reopen" << effects << strip->height();
            return false;
        }
        strip->setProperty("dismissed", true);
        processWindowEventsFor(35ms);
        if (motion->property("exit").toInt() > 0)
        {
            if (strip->height() <= 0 || strip->height() >= 44 || !shield->isVisible() || !shield->isEnabled()
                || viewport->mapToScene({0, viewport->height()}).y() > strip->mapToScene({0, 0}).y() + 1)
            {
                qWarning() << "Retained status motion hit region" << effects << motion->property("exit")
                           << strip->height() << shield->isVisible() << shield->isEnabled()
                           << viewport->mapToScene({0, viewport->height()}).y() << strip->mapToScene({0, 0}).y();
                return false;
            }
        }
        else if (strip->height() != 0)
            return false;
        if (!processWindowEventsUntil(
                [&] {
                    return strip->height() == 0 && !shield->isVisible();
                },
                1s))
        {
            qWarning() << "Retained status did not finish dismissal" << effects << strip->height()
                       << shield->isVisible();
            return false;
        }
        qInfo() << "Retained status motion and shielding passed" << effects << motion->property("exit");
    }
    window.rootObject()->setProperty("appearancePreviewActive", false);
    strip->setProperty("dismissed", false);
    if (!controller.saveTerminalTheme(QStringLiteral("ztermy-dark")))
        return false;
    processWindowEventsFor(300ms);
    if (!captureWindowSmokeItem(window.contentItem(), QStringLiteral("retained-terminal-dark.png")))
        return false;

    // Retention belongs to a Pane, not only the active Tab/main-window route.
    const auto viewName = viewport->objectName().toLatin1();
    if (!controller.splitActiveTerminal(QStringLiteral("horizontal"), false, {}, QStringLiteral("commandPrompt")))
        return false;
    if (!processWindowEventsUntil(
            [&] {
                // Split/move recreates QML leaves; Pane identity stays stable.
                viewport = qobject_cast<TerminalItem *>(quickItem(window.rootObject(), viewName.constData()));
                const auto workspace = controller.terminalWorkspace(id);
                const auto liveName =
                    (QStringLiteral("terminalViewport-") + workspace.value(QStringLiteral("activePaneId")).toString())
                        .toLatin1();
                auto *live = qobject_cast<TerminalItem *>(quickItem(window.rootObject(), liveName.constData()));
                return viewport && viewport->readOnly() && live && !live->readOnly()
                       && workspace.value(QStringLiteral("paneCount")).toInt() == 2;
            },
            3s)
        || !captureWindowSmokeItem(window.contentItem(), QStringLiteral("retained-terminal-multipane.png")))
        return false;
    if (!controller.detachTerminalWorkspace(id))
        return false;
    processWindowEventsFor(400ms);
    const auto owner = controller.terminalWorkspace(id).value(QStringLiteral("windowId")).toString();
    QPointer<QQuickWindow> detached;
    for (auto *candidate : QGuiApplication::allWindows())
        if (candidate->property("ownerWindowId").toString() == owner)
            detached = qobject_cast<QQuickWindow *>(candidate);
    if (!detached)
        return false;
    detached->raise();
    detached->requestActivate();
    const auto detachedHandle = reinterpret_cast<HWND>(detached->winId()); // NOLINT(performance-no-int-to-ptr)
    SetForegroundWindow(detachedHandle);
    if (!processWindowEventsUntil(
            [&] {
                return detached->isActive();
            },
            2s))
        return false;
    auto *retained = qobject_cast<TerminalItem *>(quickItem(detached->contentItem(), viewName.constData()));
    if (!retained || !retained->readOnly())
        return false;
    retained->forceActiveFocus();
    processWindowEventsFor(200ms);
    retained = qobject_cast<TerminalItem *>(quickItem(detached->contentItem(), viewName.constData()));
    if (!retained || !retained->hasActiveFocus())
        return false;
    retained->selectAllTerminal();
    if (!processWindowEventsUntil(
            [&] {
                return retained->hasSelection();
            },
            3s))
        return false;
    const auto pendingCopy = QStringLiteral("ZTERMY_RETAINED_COPY_PENDING");
    retained->setClipboardText(pendingCopy);
    if (!processWindowEventsUntil(
            [&] {
                auto *clipboard = QGuiApplication::clipboard();
                return clipboard->ownsClipboard() && clipboard->text() == pendingCopy;
            },
            2s))
    {
        qWarning() << "Retained detached fixture cannot establish clipboard ownership";
        return false;
    }
    // Deliver the clipboard-change notifications from the fixture's own
    // sentinel write before asking a different window to replace its data.
    processWindowEventsFor(100ms);
    qt_handleKeyEvent(detached, QEvent::KeyPress, Qt::Key_Insert, Qt::ControlModifier);
    qt_handleKeyEvent(detached, QEvent::KeyRelease, Qt::Key_Insert, Qt::ControlModifier);
    if (!processWindowEventsUntil(
            [] {
                auto *clipboard = QGuiApplication::clipboard();
                return clipboard->ownsClipboard() && clipboard->text().contains(QStringLiteral("ZTERMY_RETAINED_60"));
            },
            3s)
        || !captureWindowSmokeItem(detached->contentItem(), QStringLiteral("retained-terminal-detached.png")))
    {
        DWORD clipboardProcess = 0;
        (void)GetWindowThreadProcessId(GetOpenClipboardWindow(), &clipboardProcess);
        qWarning() << "Retained detached clipboard gate failed" << "openClipboardProcess=" << clipboardProcess;
        return false;
    }
    qInfo() << "Retained terminal: real CMD exit, reserved status, native copy shortcut, search and dismissal passed";
    return true;
}
} // namespace ztermy::ui
