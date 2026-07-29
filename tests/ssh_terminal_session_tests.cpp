#include "application/ssh/SshTerminalSession.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>
#include <type_traits>
#include <utility>

using namespace std::chrono_literals;

class SshTerminalSessionTests final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsInvalidStartupConfiguration();
    void sensitiveCredentialsAreMoveOnlyAndClearable();
    void ignoresTerminalInteractionWhileDisconnected();
    void connectsAfterExplicitHostKeyConfirmation();
};

void SshTerminalSessionTests::rejectsInvalidStartupConfiguration()
{
    ztermy::ssh::SshTerminalSession session;

    auto invalidProfile = session.start({}, {.columns = 80, .rows = 24});
    QCOMPARE(invalidProfile, std::make_error_code(std::errc::invalid_argument));

    ztermy::ssh::SshConnectionRequest profile{
        .host = QStringLiteral("server.example.test"),
        .port = 22,
        .username = QStringLiteral("user"),
        .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
        .privateKeyPath = QStringLiteral("key"),
        .knownHostsPath = QStringLiteral("known_hosts.json"),
    };
    auto invalidGeometry = session.start(std::move(profile), {.columns = 0, .rows = 24});
    QCOMPARE(invalidGeometry, std::make_error_code(std::errc::invalid_argument));

    ztermy::ssh::SshConnectionRequest emptyPassword{
        .host = QStringLiteral("server.example.test"),
        .port = 22,
        .username = QStringLiteral("user"),
        .authentication = ztermy::ssh::SshAuthenticationMethod::Password,
        .knownHostsPath = QStringLiteral("known_hosts.json"),
    };
    QCOMPARE(session.start(std::move(emptyPassword), {.columns = 80, .rows = 24}),
             std::make_error_code(std::errc::invalid_argument));
}

void SshTerminalSessionTests::sensitiveCredentialsAreMoveOnlyAndClearable()
{
    static_assert(!std::is_copy_constructible_v<ztermy::security::SensitiveByteArray>);
    static_assert(!std::is_copy_assignable_v<ztermy::security::SensitiveByteArray>);

    ztermy::security::SensitiveByteArray secret(QByteArrayLiteral("temporary-secret"));
    QVERIFY(secret.view() == std::string_view("temporary-secret"));

    ztermy::security::SensitiveByteArray moved(std::move(secret));
    QVERIFY(moved.view() == std::string_view("temporary-secret"));
    moved.clear();
    QVERIFY(moved.empty());
}

void SshTerminalSessionTests::ignoresTerminalInteractionWhileDisconnected()
{
    ztermy::ssh::SshTerminalSession session;
    QSignalSpy snapshotSpy(&session, &ztermy::ssh::SshTerminalSession::snapshotReady);
    QSignalSpy clipboardSpy(&session, &ztermy::ssh::SshTerminalSession::clipboardTextReady);

    session.queueInput(QByteArrayLiteral("input"));
    session.queuePaste(QByteArrayLiteral("paste"));
    session.requestScroll(-3);
    session.requestSelection(0, 0, 4, 0, false);
    session.clearSelection();
    session.copySelection();

    QCOMPARE(snapshotSpy.count(), 0);
    QCOMPARE(clipboardSpy.count(), 0);
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

    const QString knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json"));
    const auto request = [&] {
        return ztermy::ssh::SshConnectionRequest{
            .host = QString::fromUtf8(host),
            .port = 22,
            .username = QString::fromUtf8(username),
            .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
            .privateKeyPath = QString::fromUtf8(privateKey),
            .knownHostsPath = knownHostsPath,
        };
    };
    QVERIFY(!session.start(request(), {.columns = 80, .rows = 24}));
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

    snapshotSpy.clear();
    session.requestSelection(0, 0, 5, 0, false);
    QTRY_VERIFY_WITH_TIMEOUT(snapshotSpy.count() > 0, 5s);
    const auto selectedSnapshot =
        qvariant_cast<ztermy::terminal::TerminalSnapshotPtr>(snapshotSpy.constLast().constFirst());
    QVERIFY(selectedSnapshot);
    for (std::uint16_t column = 0; column <= 5; ++column)
    {
        QVERIFY(selectedSnapshot->cell(column, 0).selected);
    }

    session.copySelection();
    session.clearSelection();
    QTRY_VERIFY_WITH_TIMEOUT(snapshotSpy.count() > 1, 5s);
    const auto clearedSnapshot =
        qvariant_cast<ztermy::terminal::TerminalSnapshotPtr>(snapshotSpy.constLast().constFirst());
    QVERIFY(clearedSnapshot);
    for (std::uint16_t column = 0; column <= 5; ++column)
    {
        QVERIFY(!clearedSnapshot->cell(column, 0).selected);
    }

    session.requestScroll(-1);
    session.requestResize(100, 30, 8, 16);
    session.stop();

    QVERIFY(QFileInfo::exists(knownHostsPath));
    confirmationSpy.clear();
    runningSpy.clear();
    snapshotSpy.clear();

    QVERIFY(!session.start(request(), {.columns = 80, .rows = 24}));
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
