#include "application/ApplicationInstance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QMetaObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>

class ApplicationInstanceTests final : public QObject
{
    Q_OBJECT

private slots:
    void activationRestoresMinimizedWindowWithoutLosingMaximizedState();
    void activationShowsHiddenWindowWithoutLosingMaximizedState();
    void directoryRequestsPreserveLiteralPathsAndRejectMissingArguments();
    void directoryRequestIsDeliveredOnceAfterStartupIsReady();
    void shellChoiceSurvivesArgumentsAndPeerDelivery();
    void malformedPeerCannotOpenDirectoryOrActivateWindow();
    void sshArgumentsAndVersionThreeRemainNonSecret();
    void rejectsConflictingAndSecretLaunchArguments();
    void detachedSshPeerIsQueuedWithoutActivatingMain();
};

void ApplicationInstanceTests::activationRestoresMinimizedWindowWithoutLosingMaximizedState()
{
    QWindow window;
    window.setWindowStates(Qt::WindowMaximized | Qt::WindowMinimized);
    window.setVisible(true);

    ztermy::ApplicationInstance instance;
    instance.setWindow(&window);
    QVERIFY(QMetaObject::invokeMethod(&instance, "activationRequested"));

    QCOMPARE(window.windowState(), Qt::WindowMaximized);
    QCOMPARE(window.windowStates(), Qt::WindowStates{Qt::WindowMaximized});
    QVERIFY(window.isVisible());
}

void ApplicationInstanceTests::activationShowsHiddenWindowWithoutLosingMaximizedState()
{
    // A second launch while the window sits in the tray must bring the window
    // back exactly as it was, not as a normal window.
    QWindow window;
    window.setWindowStates(Qt::WindowMaximized);
    window.setVisible(true);
    window.hide();

    ztermy::ApplicationInstance instance;
    instance.setWindow(&window);
    QVERIFY(QMetaObject::invokeMethod(&instance, "activationRequested"));

    QVERIFY(window.isVisible());
    QCOMPARE(window.windowStates(), Qt::WindowStates{Qt::WindowMaximized});
}

void ApplicationInstanceTests::directoryRequestsPreserveLiteralPathsAndRejectMissingArguments()
{
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("中文 space & % !"));
    QVERIFY(QDir().mkpath(path));
    const auto request = ztermy::config::ApplicationLaunchRequest::fromArguments(
        {QStringLiteral("ztermy"), QStringLiteral("--background"), QStringLiteral("--open-directory"), path});
    QVERIFY(request);
    QCOMPARE(request->directory, path);
    QVERIFY(!request->background);
    const auto received = ztermy::config::ApplicationLaunchRequest::fromMessage(request->toMessage());
    QVERIFY(received);
    QCOMPARE(received->directory, path);
    for (const auto &arguments : {QStringList{QStringLiteral("ztermy"), QStringLiteral("--open-directory")},
                                  QStringList{QStringLiteral("ztermy"), QStringLiteral("--open-directory=")},
                                  QStringList{QStringLiteral("ztermy"), QStringLiteral("--open-directory"),
                                              directory.filePath(QStringLiteral("missing"))},
                                  QStringList{QStringLiteral("ztermy"), QStringLiteral("--open-directory"), path,
                                              QStringLiteral("--open-directory"), path}})
        QVERIFY(!ztermy::config::ApplicationLaunchRequest::fromArguments(arguments));
    QVERIFY(!ztermy::config::ApplicationLaunchRequest::fromMessage(
        QByteArrayLiteral("{\"version\":1,\"directory\":\"relative\",\"background\":false}\n")));
    QVERIFY(!ztermy::config::ApplicationLaunchRequest::fromMessage(QByteArray(65537, 'x')));
}

namespace
{
QString endpointFor(const QString &path)
{
    const auto canonical = QFileInfo(path).canonicalFilePath();
    return QStringLiteral("ztermy-")
           + QString::fromLatin1(
               QCryptographicHash::hash(canonical.toCaseFolded().toUtf8(), QCryptographicHash::Sha256).toHex());
}
} // namespace

void ApplicationInstanceTests::directoryRequestIsDeliveredOnceAfterStartupIsReady()
{
    QTemporaryDir directory;
    ztermy::ApplicationInstance instance;
    QCOMPARE(instance.claim(directory.path()), ztermy::ApplicationInstance::Result::Primary);
    QSignalSpy opened(&instance, &ztermy::ApplicationInstance::directoryOpenRequested);
    QSignalSpy activated(&instance, &ztermy::ApplicationInstance::activationRequested);
    QLocalSocket peer;
    peer.connectToServer(endpointFor(directory.path()));
    QTRY_COMPARE(peer.state(), QLocalSocket::ConnectedState);
    const ztermy::config::ApplicationLaunchRequest request{.directory = directory.path()};
    peer.write(request.toMessage());
    QTRY_VERIFY(peer.bytesAvailable() > 0);
    QCOMPARE(peer.readAll(), QByteArrayLiteral("ok\n"));
    QCOMPARE(opened.count(), 0);
    QCOMPARE(activated.count(), 0);
    instance.setReady();
    QCOMPARE(opened.count(), 1);
    QCOMPARE(opened.at(0).at(0).toString(), directory.path());
    QCOMPARE(opened.at(0).at(1).toString(), QString{});
    QCOMPARE(activated.count(), 1);
    instance.setReady();
    QCOMPARE(opened.count(), 1);
    QCOMPARE(activated.count(), 1);
}

void ApplicationInstanceTests::shellChoiceSurvivesArgumentsAndPeerDelivery()
{
    QTemporaryDir directory;
    const auto request = ztermy::config::ApplicationLaunchRequest::fromArguments(
        {QStringLiteral("ztermy"), QStringLiteral("--local-shell=commandPrompt"), QStringLiteral("--open-directory"),
         directory.path()});
    QVERIFY(request);
    QCOMPARE(request->shellId, QStringLiteral("commandPrompt"));
    const auto message = ztermy::config::ApplicationLaunchRequest::fromMessage(request->toMessage());
    QVERIFY(message);
    QCOMPARE(message->shellId, request->shellId);
    QVERIFY(ztermy::config::ApplicationLaunchRequest::fromMessage(
        QByteArrayLiteral("{\"version\":1,\"directory\":\"C:/\",\"background\":false}\n")));
    for (const auto &arguments :
         {QStringList{QStringLiteral("ztermy"), QStringLiteral("--local-shell")},
          QStringList{QStringLiteral("ztermy"), QStringLiteral("--local-shell=arbitrary.exe"),
                      QStringLiteral("--open-directory"), directory.path()},
          QStringList{QStringLiteral("ztermy"), QStringLiteral("--local-shell=commandPrompt")},
          QStringList{QStringLiteral("ztermy"), QStringLiteral("--local-shell=commandPrompt"),
                      QStringLiteral("--local-shell=gitBash"), QStringLiteral("--open-directory"), directory.path()}})
        QVERIFY(!ztermy::config::ApplicationLaunchRequest::fromArguments(arguments));
    QVERIFY(!ztermy::config::ApplicationLaunchRequest::fromMessage(
        QByteArrayLiteral("{\"version\":2,\"directory\":\"C:/\",\"background\":false,\"shellId\":\"evil.exe\"}")));
    QVERIFY(!ztermy::config::ApplicationLaunchRequest::fromMessage(
        QByteArrayLiteral("{\"version\":2,\"directory\":\"\",\"background\":false,\"shellId\":\"commandPrompt\"}")));
    ztermy::ApplicationInstance instance;
    QCOMPARE(instance.claim(directory.path()), ztermy::ApplicationInstance::Result::Primary);
    QSignalSpy opened(&instance, &ztermy::ApplicationInstance::directoryOpenRequested);
    QLocalSocket peer;
    peer.connectToServer(endpointFor(directory.path()));
    QTRY_COMPARE(peer.state(), QLocalSocket::ConnectedState);
    peer.write(request->toMessage());
    QTRY_VERIFY(peer.bytesAvailable() > 0);
    QCOMPARE(peer.readAll(), QByteArrayLiteral("ok\n"));
    QCOMPARE(opened.count(), 0);
    instance.setReady();
    QCOMPARE(opened.count(), 1);
    QCOMPARE(opened.first().at(1).toString(), request->shellId);
}

void ApplicationInstanceTests::malformedPeerCannotOpenDirectoryOrActivateWindow()
{
    QTemporaryDir directory;
    ztermy::ApplicationInstance instance;
    QCOMPARE(instance.claim(directory.path()), ztermy::ApplicationInstance::Result::Primary);
    instance.setReady();
    QSignalSpy opened(&instance, &ztermy::ApplicationInstance::directoryOpenRequested);
    QSignalSpy activated(&instance, &ztermy::ApplicationInstance::activationRequested);
    QLocalSocket peer;
    peer.connectToServer(endpointFor(directory.path()));
    QTRY_COMPARE(peer.state(), QLocalSocket::ConnectedState);
    peer.write("{\"version\":99,\"directory\":\"C:/\",\"background\":false}\n");
    QTRY_COMPARE(peer.state(), QLocalSocket::UnconnectedState);
    QCOMPARE(opened.count(), 0);
    QCOMPARE(activated.count(), 0);
}

void ApplicationInstanceTests::sshArgumentsAndVersionThreeRemainNonSecret()
{
    using Request = ztermy::config::ApplicationLaunchRequest;
    const auto launch = Request::fromArguments(
        {QStringLiteral("ztermy"), QStringLiteral("ssh://test@[::1]:2222/home/test%20dir"),
         QStringLiteral("--window=detached"), QStringLiteral("--identity=key.pem"),
         QStringLiteral("--certificate=key-cert.pub"), QStringLiteral("--password-file=credential.txt")});
    QVERIFY(launch);
    QCOMPARE(launch->host, QStringLiteral("::1"));
    QCOMPARE(launch->username, QStringLiteral("test"));
    QCOMPARE(launch->port, 2222);
    QCOMPARE(launch->authentication, QStringLiteral("private-key"));
    QCOMPARE(launch->remoteDirectory, QStringLiteral("/home/test dir"));
    QCOMPARE(launch->windowMode, QStringLiteral("detached"));
    QVERIFY(QDir::isAbsolutePath(launch->passwordFile));
    const auto received = Request::fromMessage(launch->toMessage());
    QVERIFY(received);
    QCOMPARE(received->toMessage(), launch->toMessage());
    QVERIFY(!launch->toMessage().contains("\"secret\""));
    auto malformed = launch->toMessage();
    malformed.replace("\"port\":2222", "\"port\":2222.5");
    QVERIFY(!Request::fromMessage(malformed));
    const auto saved =
        Request::fromArguments({QStringLiteral("ztermy"), QStringLiteral("--profile=known-host-id"),
                                QStringLiteral("--window=detached"), QStringLiteral("--remote-directory=/srv")});
    QVERIFY(saved);
    QCOMPARE(saved->profileId, QStringLiteral("known-host-id"));
    QVERIFY(Request::fromMessage(saved->toMessage()));
}

void ApplicationInstanceTests::rejectsConflictingAndSecretLaunchArguments()
{
    using Request = ztermy::config::ApplicationLaunchRequest;
    const QStringList base{QStringLiteral("ztermy"), QStringLiteral("--ssh=test@example.org")};
    for (const auto &extra :
         {QStringList{QStringLiteral("--window=invalid")}, QStringList{QStringLiteral("--auth=automatic")},
          QStringList{QStringLiteral("--auth=private-key")},
          QStringList{QStringLiteral("--certificate=certificate.pub")}, QStringList{QStringLiteral("--profile=id")},
          QStringList{QStringLiteral("--port=0")}, QStringList{QStringLiteral("--port=65536")},
          QStringList{QStringLiteral("--port=word")}, QStringList{QStringLiteral("--user=duplicate")},
          QStringList{QStringLiteral("--auth=agent"), QStringLiteral("--password-file=file.txt")},
          QStringList{QStringLiteral("--password=never-log-this-value")},
          QStringList{QStringLiteral("-pw"), QStringLiteral("never-log-this-value")},
          QStringList{QStringLiteral("--password-file=//server/share/file")}, QStringList{QStringLiteral("--identity")},
          QStringList{QStringLiteral("--window=main"), QStringLiteral("--window=detached")}})
    {
        const auto rejected = Request::fromArguments(base + extra);
        QVERIFY(!rejected);
        QVERIFY(!rejected.error().contains(QStringLiteral("never-log-this-value")));
    }
    for (const auto &target :
         {QStringLiteral("ssh://test:never-log-this-value@example.org"), QStringLiteral("ssh://test@example.org/?x=1"),
          QStringLiteral("ssh://example.org"), QStringLiteral("ssh://test@example.org/path\ncommand")})
        QVERIFY(!Request::fromArguments({QStringLiteral("ztermy"), target}));
    QVERIFY(!Request::fromArguments({QStringLiteral("ztermy"), QStringLiteral("--window=detached")}));
    QVERIFY(!Request::fromArguments({QStringLiteral("ztermy"), QStringLiteral("--port=22")}));
    QVERIFY(!Request::fromArguments(
        {QStringLiteral("ztermy"), QStringLiteral("--profile=id"), QStringLiteral("--port=22")}));
    const auto separated = Request::fromArguments({QStringLiteral("ztermy"), QStringLiteral("--ssh=example.org"),
                                                   QStringLiteral("--user=test"), QStringLiteral("--port=2222"),
                                                   QStringLiteral("--auth=agent")});
    QVERIFY(separated);
    QCOMPARE(separated->port, 2222);
}

void ApplicationInstanceTests::detachedSshPeerIsQueuedWithoutActivatingMain()
{
    QTemporaryDir directory;
    ztermy::ApplicationInstance instance;
    QCOMPARE(instance.claim(directory.path()), ztermy::ApplicationInstance::Result::Primary);
    QSignalSpy launched(&instance, &ztermy::ApplicationInstance::launchRequested);
    QSignalSpy activated(&instance, &ztermy::ApplicationInstance::activationRequested);
    QLocalSocket peer;
    peer.connectToServer(endpointFor(directory.path()));
    QTRY_COMPARE(peer.state(), QLocalSocket::ConnectedState);
    const auto request = ztermy::config::ApplicationLaunchRequest::fromArguments(
        {QStringLiteral("ztermy"), QStringLiteral("--ssh=user@example.org"), QStringLiteral("--window=detached")});
    QVERIFY(request);
    peer.write(request->toMessage());
    QTRY_VERIFY(peer.bytesAvailable() > 0);
    QCOMPARE(peer.readAll(), QByteArrayLiteral("ok\n"));
    QCOMPARE(launched.count(), 0);
    instance.setReady();
    QCOMPARE(launched.count(), 1);
    const auto forwarded = qvariant_cast<ztermy::config::ApplicationLaunchRequest>(launched.first().first());
    QCOMPARE(forwarded.host, request->host);
    QCOMPARE(forwarded.windowMode, request->windowMode);
    QCOMPARE(activated.count(), 0);
    instance.setReady();
    QCOMPARE(launched.count(), 1);
}

QTEST_MAIN(ApplicationInstanceTests)

#include "application_instance_tests.moc"
