#include "application/ssh/SshTerminalSession.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>

using namespace std::chrono_literals;

class SshTerminalSessionTests final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsInvalidStartupConfiguration();
    void connectsAfterExplicitHostKeyConfirmation();
};

void SshTerminalSessionTests::rejectsInvalidStartupConfiguration()
{
    ztermy::ssh::SshTerminalSession session;

    auto invalidProfile = session.start({}, {.columns = 80, .rows = 24});
    QCOMPARE(invalidProfile, std::make_error_code(std::errc::invalid_argument));

    const ztermy::ssh::SshPrivateKeyProfile profile{
        .host = QStringLiteral("server.example.test"),
        .port = 22,
        .username = QStringLiteral("user"),
        .privateKeyPath = QStringLiteral("key"),
        .knownHostsPath = QStringLiteral("known_hosts.json"),
    };
    auto invalidGeometry = session.start(profile, {.columns = 0, .rows = 24});
    QCOMPARE(invalidGeometry, std::make_error_code(std::errc::invalid_argument));
}

void SshTerminalSessionTests::connectsAfterExplicitHostKeyConfirmation()
{
    const QByteArray host = qgetenv("ZTERMY_TEST_SSH_HOST");
    const QByteArray username = qgetenv("ZTERMY_TEST_SSH_USERNAME");
    const QByteArray privateKey = qgetenv("ZTERMY_TEST_SSH_PRIVATE_KEY");
    const QByteArray expectedFingerprint = qgetenv("ZTERMY_TEST_SSH_EXPECTED_FINGERPRINT");
    if (host.isEmpty() || username.isEmpty() || privateKey.isEmpty() || expectedFingerprint.isEmpty())
    {
        QSKIP("Set the real-host private-key gate variables to run the SSH application session test");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    ztermy::ssh::SshTerminalSession session;
    QSignalSpy confirmationSpy(&session, &ztermy::ssh::SshTerminalSession::hostKeyConfirmationRequired);
    QSignalSpy runningSpy(&session, &ztermy::ssh::SshTerminalSession::runningChanged);
    QSignalSpy snapshotSpy(&session, &ztermy::ssh::SshTerminalSession::snapshotReady);
    session.confirmHostKey(true);

    const ztermy::ssh::SshPrivateKeyProfile profile{
        .host = QString::fromUtf8(host),
        .port = 22,
        .username = QString::fromUtf8(username),
        .privateKeyPath = QString::fromUtf8(privateKey),
        .knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json")),
    };
    QVERIFY(!session.start(profile, {.columns = 80, .rows = 24}));
    QTRY_COMPARE_WITH_TIMEOUT(confirmationSpy.count(), 1, 10s);

    const QList<QVariant> confirmation = confirmationSpy.takeFirst();
    QCOMPARE(confirmation.at(1).toString(), QString::fromLatin1(expectedFingerprint));
    session.confirmHostKey(true);

    QTRY_VERIFY_WITH_TIMEOUT(
        [&runningSpy] {
            for (const QList<QVariant> &arguments : runningSpy)
            {
                if (arguments.at(0).toBool())
                {
                    return true;
                }
            }
            return false;
        }(),
        15s);
    QTRY_VERIFY_WITH_TIMEOUT(snapshotSpy.count() > 0, 5s);

    session.requestResize(100, 30, 8, 16);
    session.stop();

    QVERIFY(QFileInfo::exists(profile.knownHostsPath));
    confirmationSpy.clear();
    runningSpy.clear();
    snapshotSpy.clear();

    QVERIFY(!session.start(profile, {.columns = 80, .rows = 24}));
    QTRY_VERIFY_WITH_TIMEOUT(
        [&runningSpy] {
            for (const QList<QVariant> &arguments : runningSpy)
            {
                if (arguments.at(0).toBool())
                {
                    return true;
                }
            }
            return false;
        }(),
        15s);
    QCOMPARE(confirmationSpy.count(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(snapshotSpy.count() > 0, 5s);
    session.stop();
}

QTEST_GUILESS_MAIN(SshTerminalSessionTests)

#include "ssh_terminal_session_tests.moc"
