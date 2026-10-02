#pragma once

#include <QCryptographicHash>
#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include "application/ApplicationInstance.h"
#include "ui/RuntimeSmokeItems.h"

namespace ztermy::ui
{
inline bool launchPeer(NativeWindow &main, const QString &dataDir, QStringList arguments)
{
    using namespace std::chrono_literals;
    arguments.prepend(dataDir);
    arguments.prepend(QStringLiteral("--data-dir"));
    QProcess peer;
    peer.start(QCoreApplication::applicationFilePath(), arguments);
    const auto stop = qScopeGuard([&] {
        if (peer.state() != QProcess::NotRunning)
        {
            peer.kill();
            peer.waitForFinished(3000);
        }
    });
    Q_UNUSED(main);
    return settleWindowUntil(
               [&] {
                   return peer.state() == QProcess::NotRunning;
               },
               15s)
           && peer.exitStatus() == QProcess::NormalExit && peer.exitCode() == 0;
}

inline QQuickWindow *launchDetachedWindow(const QString &owner)
{
    for (auto *window : QGuiApplication::allWindows())
        if (window->objectName() == QStringLiteral("detachedTerminalWindow")
            && window->property("ownerWindowId").toString() == owner)
            return qobject_cast<QQuickWindow *>(window);
    return nullptr;
}

inline bool verifyLaunchRuntime(NativeWindow &main, AppController &controller, const QDir &captures,
                                const QString &dataDir)
{
    using namespace std::chrono_literals;
    const auto count = controller.terminalTabs().size();
    main.hide();
    if (!launchPeer(main, dataDir,
                    {QStringLiteral("--open-directory"), captures.path(), QStringLiteral("--local-shell=commandPrompt"),
                     QStringLiteral("--window=detached")}))
        return false;
    if (!settleWindowUntil(
            [&] {
                return controller.terminalTabs().size() == count + 1;
            },
            3s))
        return false;
    const auto localId = controller.activeTerminalTabId();
    const auto owner = controller.terminalWorkspace(localId).value(QStringLiteral("windowId")).toString();
    if (!settleWindowUntil(
            [&] {
                const auto *window = launchDetachedWindow(owner);
                return window && window->isVisible();
            },
            4s))
        return false;
    auto *detached = launchDetachedWindow(owner);
    // Visibility precedes the first scene-graph frame. Do not accept a blank
    // early grab as visual evidence of a successfully opened terminal.
    processWindowEventsFor(500ms);
    if (main.isVisible()
        || !detached->grabWindow().save(captures.filePath(QStringLiteral("launch-local-detached.png"))))
        return false;
    if (!controller.closeTerminalTab(localId))
        return false;
    if (!launchPeer(main, dataDir, {QStringLiteral("--ssh=fixture@127.0.0.1:1")}))
        return false;
    auto *dialog = main.rootObject()->findChild<QObject *>(QStringLiteral("launchAuthenticationDialog"));
    if (!settleWindowUntil(
            [&] {
                return dialog && dialog->property("opened").toBool();
            },
            4s))
        return false;
    if (!main.grabWindow().save(captures.filePath(QStringLiteral("launch-authentication.png"))))
        return false;
    auto *cancel = quickItem(main.rootObject(), "launchAuthenticationCancel");
    if (!focusItem(main, cancel, QStringLiteral("launchAuthenticationCancel")))
        return false;
    sendKey(main, Qt::Key_Space);
    processWindowEventsFor(350ms);
    if (dialog->property("visible").toBool() || controller.terminalTabs().size() != count)
        return false;
    if (!launchPeer(main, dataDir, {QStringLiteral("--ssh=first@127.0.0.1:1")})
        || !launchPeer(main, dataDir, {QStringLiteral("--ssh=second@127.0.0.1:1")}))
        return false;
    processWindowEventsFor(350ms);
    auto *credential = quickItem(main.rootObject(), "launchCredential");
    auto *confirm = quickItem(main.rootObject(), "launchAuthenticationConfirm");
    if (!credential || !credential->setProperty("text", QStringLiteral("fixture-only"))
        || !focusItem(main, confirm, QStringLiteral("launchAuthenticationConfirm")))
        return false;
    sendKey(main, Qt::Key_Space);
    if (!settleWindowUntil(
            [&] {
                return dialog->property("opened").toBool()
                       && dialog->property("details").toMap().value(QStringLiteral("target")).toString()
                              == QStringLiteral("second@127.0.0.1:1");
            },
            4s)
        || !focusItem(main, cancel, QStringLiteral("launchAuthenticationCancel")))
        return false;
    const auto first = controller.activeTerminalTabId();
    sendKey(main, Qt::Key_Space);
    processWindowEventsFor(350ms);
    if (dialog->property("visible").toBool() || !controller.closeTerminalTab(first)
        || controller.terminalTabs().size() != count)
        return false;
    qInfo() << "Launch runtime: second-process detached directory, hidden main preserved, credential prompt and "
               "keyboard cancellation, serialized dialogs across exit motion: true";
    return true;
}

inline bool verifySshLaunchRuntime(NativeWindow &main, AppController &controller, const QDir &captures,
                                   const QString &dataDir)
{
    using namespace std::chrono_literals;
    const auto python = qEnvironmentVariable("ZTERMY_TEST_SSH_FIXTURE_PYTHON");
    const auto fixture = qEnvironmentVariable("ZTERMY_TEST_LAUNCH_SSH_FIXTURE");
    if (python.isEmpty() || fixture.isEmpty())
    {
        qInfo() << "Loopback SSH launch acceptance not requested (fixture environment absent)";
        return true;
    }
    const auto secretPath = captures.filePath(QStringLiteral("fixture-credential.txt"));
    const auto keyPath = captures.filePath(QStringLiteral("fixture-key.pem"));
    const auto certificatePath = keyPath + QStringLiteral("-cert.pub");
    const auto removeFixture = qScopeGuard([&] {
        QFile::remove(secretPath);
        QFile::remove(keyPath);
        QFile::remove(certificatePath);
    });
    QFile secret(secretPath);
    if (!secret.open(QIODevice::WriteOnly) || secret.write("fixture-only\r\n") != 14)
        return false;
    secret.close();
    for (const auto *method : {"password", "private-key", "certificate"})
    {
        QProcess server;
        server.start(python, {fixture, QStringLiteral("--method"), QString::fromLatin1(method),
                              QStringLiteral("--key-file"), keyPath});
        const auto stop = qScopeGuard([&] {
            server.kill();
            server.waitForFinished(3000);
        });
        QByteArray events;
        if (!settleWindowUntil(
                [&] {
                    events += server.readAllStandardOutput();
                    return events.contains('\n');
                },
                15s))
            return false;
        const auto port = events.split('\n').first().sliced(5).toInt();
        if (port <= 0)
            return false;
        QStringList arguments{QStringLiteral("--ssh=fixture@127.0.0.1"),
                              QStringLiteral("--port=%1").arg(port),
                              QStringLiteral("--password-file"),
                              secretPath,
                              QStringLiteral("--window=detached"),
                              QStringLiteral("--remote-directory=/srv/中文 space ' & $(ignored)")};
        if (QString::fromLatin1(method) != QStringLiteral("password"))
            arguments << QStringLiteral("--identity") << keyPath;
        if (QString::fromLatin1(method) == QStringLiteral("certificate"))
            arguments << QStringLiteral("--certificate") << certificatePath;
        const auto before = controller.terminalTabs().size();
        if (!launchPeer(main, dataDir, arguments)
            || !settleWindowUntil(
                [&] {
                    return controller.hostKeyPromptVisible();
                },
                10s)
            || controller.terminalTabs().size() != before + 1)
            return false;
        // Only this owned loopback fixture is accepted; real launch paths never bypass verification.
        if (!main.grabWindow().save(
                captures.filePath(QStringLiteral("launch-host-key-%1.png").arg(QLatin1StringView{method}))))
            return false;
        controller.acceptHostKey(false);
        const auto expected =
            QCryptographicHash::hash(QStringLiteral("cd -- '/srv/中文 space '\\'' & $(ignored)'\r").toUtf8(),
                                     QCryptographicHash::Sha256)
                .toHex();
        if (!settleWindowUntil(
                [&] {
                    events += server.readAllStandardOutput();
                    return events.contains("STARTUP_SHA256 " + expected);
                },
                10s)
            || !events.contains("AUTH_OK"))
        {
            // The owned fixture reports only markers/public key types and digests.
            qWarning() << "Loopback fixture authentication failed:" << method << events
                       << server.readAllStandardError();
            return false;
        }
        const auto id = controller.activeTerminalTabId();
        const auto owner = controller.terminalWorkspace(id).value(QStringLiteral("windowId")).toString();
        auto *detached = launchDetachedWindow(owner);
        if (!detached || !detached->isVisible())
            return false;
        processWindowEventsFor(500ms);
        if (!detached->grabWindow().save(
                captures.filePath(QStringLiteral("launch-ssh-%1.png").arg(QLatin1StringView{method})))
            || !controller.hostProfiles().isEmpty() || !controller.closeTerminalTab(id))
            return false;
        qInfo() << "Loopback SSH launch: authentication, normal host-key confirmation, safely quoted remote directory, "
                   "detached window:"
                << method << true;
    }
    return true;
}
inline int runLaunchRuntimeSmoke(NativeWindow &window, AppController &controller, ApplicationInstance &instance,
                                 const QString &dataDirectory)
{
    showForRuntimeSmoke(window);
    QObject::connect(&instance, &ApplicationInstance::launchRequested, &controller,
                     [&controller](const config::ApplicationLaunchRequest &request) {
                         (void)controller.openLaunchRequest(request);
                     });
    instance.setReady();
    QDir captures(QDir(dataDirectory).filePath(QStringLiteral("launch-captures")));
    const bool passed = captures.mkpath(QStringLiteral("."))
                        && verifyLaunchRuntime(window, controller, captures, dataDirectory)
                        && verifySshLaunchRuntime(window, controller, captures, dataDirectory);
    controller.shutdown();
    window.releaseResources();
    qInfo() << "Launch runtime smoke passed=" << passed;
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
} // namespace ztermy::ui
