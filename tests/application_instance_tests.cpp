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
    void malformedPeerCannotOpenDirectoryOrActivateWindow();
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
    QCOMPARE(activated.count(), 1);
    instance.setReady();
    QCOMPARE(opened.count(), 1);
    QCOMPARE(activated.count(), 1);
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

QTEST_MAIN(ApplicationInstanceTests)

#include "application_instance_tests.moc"
