#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "application/ssh/SshTerminalSession.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QGlobalStatic>
#include <QHostAddress>
#include <QMutex>
#include <QSet>
#include <QSignalSpy>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <type_traits>
#include <utility>

using namespace std::chrono_literals;

namespace
{

Q_GLOBAL_STATIC(QMutex, capturedMessageMutex)
Q_GLOBAL_STATIC(QStringList, capturedMessages)
QtMessageHandler previousMessageHandler = nullptr;

class ScopedConsoleMode final
{
public:
    explicit ScopedConsoleMode(const HANDLE handle) : m_handle(handle)
    {
        m_valid = m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE
                  && GetConsoleMode(m_handle, &m_originalMode) != FALSE;
    }

    ~ScopedConsoleMode()
    {
        if (m_changed)
        {
            SetConsoleMode(m_handle, m_originalMode);
        }
    }

    ScopedConsoleMode(const ScopedConsoleMode &) = delete;
    ScopedConsoleMode &operator=(const ScopedConsoleMode &) = delete;

    [[nodiscard]] bool hideEcho()
    {
        if (!m_valid)
        {
            return false;
        }
        m_changed = SetConsoleMode(m_handle, m_originalMode & ~(ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT)) != FALSE;
        return m_changed;
    }

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
    DWORD m_originalMode = 0;
    bool m_valid = false;
    bool m_changed = false;
};

void drainConsoleLine(const HANDLE input) noexcept
{
    std::array<wchar_t, 128> discard{};
    for (;;)
    {
        DWORD charactersRead = 0;
        const BOOL read =
            ReadConsoleW(input, discard.data(), static_cast<DWORD>(discard.size()), &charactersRead, nullptr);
        const auto inputEnd = discard.begin() + static_cast<std::ptrdiff_t>(charactersRead);
        const bool reachedLineEnd = std::ranges::any_of(discard.begin(), inputEnd, [](const wchar_t character) {
            return character == L'\r' || character == L'\n';
        });
        SecureZeroMemory(discard.data(), sizeof(discard));
        if (read == FALSE || reachedLineEnd)
        {
            return;
        }
    }
}

[[nodiscard]] std::expected<ztermy::security::SensitiveByteArray, QString> readHiddenConsolePassword()
{
    constexpr DWORD maximumPasswordCharacters = 1024;
    std::array<wchar_t, maximumPasswordCharacters + 2> utf16Password{};
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    ScopedConsoleMode consoleMode(input);
    if (!consoleMode.hideEcho())
    {
        return std::unexpected(QStringLiteral("Password gate requires an interactive Windows console"));
    }

    std::fputs("SSH test password (input hidden): ", stdout);
    std::fflush(stdout);
    DWORD charactersRead = 0;
    const BOOL read =
        ReadConsoleW(input, utf16Password.data(), static_cast<DWORD>(utf16Password.size()), &charactersRead, nullptr);
    std::fputc('\n', stdout);
    std::fflush(stdout);
    if (read == FALSE)
    {
        SecureZeroMemory(utf16Password.data(), sizeof(utf16Password));
        return std::unexpected(QStringLiteral("Could not read the password from the Windows console"));
    }

    const auto inputEnd = utf16Password.begin() + static_cast<std::ptrdiff_t>(charactersRead);
    const bool cancelled = std::ranges::find(utf16Password.begin(), inputEnd, L'\x0003') != inputEnd;
    const bool completeLine =
        charactersRead > 0
        && (utf16Password.at(charactersRead - 1) == L'\r' || utf16Password.at(charactersRead - 1) == L'\n');
    if (cancelled)
    {
        SecureZeroMemory(utf16Password.data(), sizeof(utf16Password));
        return std::unexpected(QStringLiteral("Password input was cancelled"));
    }
    if (!completeLine)
    {
        drainConsoleLine(input);
    }

    while (charactersRead > 0
           && (utf16Password.at(charactersRead - 1) == L'\r' || utf16Password.at(charactersRead - 1) == L'\n'))
    {
        --charactersRead;
    }
    if (!completeLine || charactersRead > maximumPasswordCharacters)
    {
        SecureZeroMemory(utf16Password.data(), sizeof(utf16Password));
        return std::unexpected(QStringLiteral("The password is too long"));
    }
    if (charactersRead == 0)
    {
        SecureZeroMemory(utf16Password.data(), sizeof(utf16Password));
        return std::unexpected(QStringLiteral("The password cannot be empty"));
    }

    const int utf8Size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, utf16Password.data(),
                                             static_cast<int>(charactersRead), nullptr, 0, nullptr, nullptr);
    if (utf8Size <= 0)
    {
        SecureZeroMemory(utf16Password.data(), sizeof(utf16Password));
        return std::unexpected(QStringLiteral("The password is not valid Unicode"));
    }

    QByteArray utf8Password(utf8Size, Qt::Uninitialized);
    const int converted =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, utf16Password.data(), static_cast<int>(charactersRead),
                            utf8Password.data(), utf8Size, nullptr, nullptr);
    SecureZeroMemory(utf16Password.data(), sizeof(utf16Password));
    if (converted != utf8Size)
    {
        SecureZeroMemory(utf8Password.data(), static_cast<SIZE_T>(utf8Password.size()));
        return std::unexpected(QStringLiteral("Could not convert the password to UTF-8"));
    }

    return ztermy::security::SensitiveByteArray(std::move(utf8Password));
}

void captureMessage(const QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    {
        const QMutexLocker locker(capturedMessageMutex());
        capturedMessages->append(message);
    }
    if (previousMessageHandler != nullptr)
    {
        previousMessageHandler(type, context, message);
    }
}

class ScopedMessageCapture final
{
public:
    ScopedMessageCapture()
    {
        {
            const QMutexLocker locker(capturedMessageMutex());
            capturedMessages->clear();
        }
        previousMessageHandler = qInstallMessageHandler(&captureMessage);
    }

    ~ScopedMessageCapture()
    {
        qInstallMessageHandler(previousMessageHandler);
        previousMessageHandler = nullptr;
    }

    ScopedMessageCapture(const ScopedMessageCapture &) = delete;
    ScopedMessageCapture &operator=(const ScopedMessageCapture &) = delete;

    [[nodiscard]] QStringList messages() const
    {
        const QMutexLocker locker(capturedMessageMutex());
        return *capturedMessages;
    }
};

} // namespace

class SshTerminalSessionTests final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsInvalidStartupConfiguration();
    void presentsDistinctFailureStatuses();
    void reportsConnectionRefusalFromLiveSocket();
    void reportsHandshakeTimeoutFromSilentPeer();
    void sensitiveCredentialsAreMoveOnlyAndClearable();
    void doesNotExposeCredentialsInStatusOrLogs();
    void ignoresTerminalInteractionWhileDisconnected();
    void connectsAfterExplicitHostKeyConfirmation();
    void reportsAuthenticationRejectionOnRealHost();
    void reportsRemoteCloseOnRealHost();
    void authenticatesWithInteractivePasswordOnRealHost();
    void measuresInteractiveInputQueueLatency();
    void survivesRepeatedConnectDisconnectCycles();
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

void SshTerminalSessionTests::presentsDistinctFailureStatuses()
{
    using ztermy::ssh::SshFailureKind;

    constexpr std::array failures = {
        SshFailureKind::NameResolutionFailed,
        SshFailureKind::ConnectionRefused,
        SshFailureKind::TimedOut,
        SshFailureKind::TransportError,
        SshFailureKind::HostKeyChanged,
        SshFailureKind::HostKeyInvalid,
        SshFailureKind::AuthenticationRejected,
        SshFailureKind::AuthenticationUnavailable,
        SshFailureKind::ChannelOpenFailed,
        SshFailureKind::RemoteClosed,
        SshFailureKind::Cancelled,
        SshFailureKind::ProtocolError,
    };

    QSet<QString> statuses;
    for (const SshFailureKind failure : failures)
    {
        const QString status = ztermy::ssh::sshFailureStatus(failure);
        QVERIFY2(!status.isEmpty(), "Every SSH failure must have a user-visible status");
        QVERIFY2(!statuses.contains(status), "SSH failure statuses must remain distinct");
        statuses.insert(status);
    }

    QCOMPARE(ztermy::ssh::sshFailureStatus(SshFailureKind::ConnectionRefused),
             QStringLiteral("SSH connection was refused"));
    QCOMPARE(ztermy::ssh::sshFailureStatus(SshFailureKind::TimedOut), QStringLiteral("SSH operation timed out"));
    QCOMPARE(ztermy::ssh::sshFailureStatus(SshFailureKind::AuthenticationRejected),
             QStringLiteral("SSH authentication was rejected"));
    QCOMPARE(ztermy::ssh::sshFailureStatus(SshFailureKind::RemoteClosed),
             QStringLiteral("SSH remote host closed the connection"));
}

void SshTerminalSessionTests::reportsConnectionRefusalFromLiveSocket()
{
    QTcpServer portReservation;
    QVERIFY(portReservation.listen(QHostAddress::LocalHost, 0));
    const quint16 refusedPort = portReservation.serverPort();
    portReservation.close();

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::ssh::SshTerminalSession session;
    QSignalSpy failureSpy(&session, &ztermy::ssh::SshTerminalSession::failureOccurred);
    QSignalSpy statusSpy(&session, &ztermy::ssh::SshTerminalSession::statusChanged);
    ztermy::ssh::SshConnectionRequest request{
        .host = QStringLiteral("127.0.0.1"),
        .port = refusedPort,
        .username = QStringLiteral("unused"),
        .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
        .privateKeyPath = QStringLiteral("unused"),
        .knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json")),
    };

    QVERIFY(!session.start(std::move(request), {.columns = 80, .rows = 24}));
    QTRY_VERIFY_WITH_TIMEOUT(std::ranges::any_of(failureSpy,
                                                 [](const QList<QVariant> &arguments) {
                                                     return !arguments.isEmpty()
                                                            && qvariant_cast<ztermy::ssh::SshFailureKind>(
                                                                   arguments.constFirst())
                                                                   == ztermy::ssh::SshFailureKind::ConnectionRefused;
                                                 }),
                             5s);
    QVERIFY(!statusSpy.isEmpty());
    QCOMPARE(statusSpy.constLast().constFirst().toString(),
             ztermy::ssh::sshFailureStatus(ztermy::ssh::SshFailureKind::ConnectionRefused));
    session.stop();
}

void SshTerminalSessionTests::reportsHandshakeTimeoutFromSilentPeer()
{
    QTcpServer silentServer;
    QVERIFY(silentServer.listen(QHostAddress::LocalHost, 0));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::ssh::SshTerminalSession session;
    QSignalSpy failureSpy(&session, &ztermy::ssh::SshTerminalSession::failureOccurred);
    QSignalSpy statusSpy(&session, &ztermy::ssh::SshTerminalSession::statusChanged);
    ztermy::ssh::SshConnectionRequest request{
        .host = QStringLiteral("127.0.0.1"),
        .port = silentServer.serverPort(),
        .username = QStringLiteral("unused"),
        .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
        .privateKeyPath = QStringLiteral("unused"),
        .knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json")),
    };

    QVERIFY(!session.start(std::move(request), {.columns = 80, .rows = 24}));
    QTRY_VERIFY_WITH_TIMEOUT(silentServer.hasPendingConnections(), 2s);
    std::unique_ptr<QTcpSocket> silentPeer(silentServer.nextPendingConnection());
    QVERIFY(silentPeer);
    QTRY_VERIFY_WITH_TIMEOUT(std::ranges::any_of(failureSpy,
                                                 [](const QList<QVariant> &arguments) {
                                                     return !arguments.isEmpty()
                                                            && qvariant_cast<ztermy::ssh::SshFailureKind>(
                                                                   arguments.constFirst())
                                                                   == ztermy::ssh::SshFailureKind::TimedOut;
                                                 }),
                             12s);
    QVERIFY(!statusSpy.isEmpty());
    QCOMPARE(statusSpy.constLast().constFirst().toString(),
             ztermy::ssh::sshFailureStatus(ztermy::ssh::SshFailureKind::TimedOut));
    session.stop();
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

void SshTerminalSessionTests::doesNotExposeCredentialsInStatusOrLogs()
{
    constexpr auto passwordSentinel = "ztermy-password-sentinel-7d9f8c2a";
    constexpr auto passphraseSentinel = "ztermy-passphrase-sentinel-41e6b3d0";
    const std::array sentinelTexts = {
        QString::fromLatin1(passwordSentinel),
        QString::fromLatin1(passphraseSentinel),
    };

    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QStringList messages;
    QList<QList<QVariant>> statuses;
    {
        ScopedMessageCapture capture;
        ztermy::ssh::SshTerminalSession session;
        QSignalSpy statusSpy(&session, &ztermy::ssh::SshTerminalSession::statusChanged);
        QSignalSpy phaseSpy(&session, &ztermy::ssh::SshTerminalSession::phaseChanged);
        const auto runFailedRequest = [&](ztermy::ssh::SshConnectionRequest request) {
            statusSpy.clear();
            phaseSpy.clear();
            QVERIFY(!session.start(std::move(request), {.columns = 80, .rows = 24}));
            QTRY_VERIFY_WITH_TIMEOUT(std::ranges::any_of(phaseSpy,
                                                         [](const QList<QVariant> &arguments) {
                                                             return !arguments.isEmpty()
                                                                    && qvariant_cast<ztermy::ssh::SshConnectionPhase>(
                                                                           arguments.constFirst())
                                                                           == ztermy::ssh::SshConnectionPhase::Failed;
                                                         }),
                                     12s);
            session.stop();
            for (const QList<QVariant> &arguments : statusSpy)
            {
                statuses.append(arguments);
            }
        };

        runFailedRequest({
            .host = QStringLiteral("127.0.0.1"),
            .port = 1,
            .username = QStringLiteral("sentinel-user"),
            .authentication = ztermy::ssh::SshAuthenticationMethod::Password,
            .secret = ztermy::security::SensitiveByteArray(QByteArray(passwordSentinel)),
            .knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json")),
        });

        runFailedRequest({
            .host = QStringLiteral("127.0.0.1"),
            .port = 1,
            .username = QStringLiteral("sentinel-user"),
            .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
            .privateKeyPath = QStringLiteral("sentinel-key-path"),
            .secret = ztermy::security::SensitiveByteArray(QByteArray(passphraseSentinel)),
            .knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json")),
        });

        messages = capture.messages();
    }

    for (const QString &sentinelText : sentinelTexts)
    {
        for (const QList<QVariant> &arguments : statuses)
        {
            QVERIFY(!arguments.constFirst().toString().contains(sentinelText));
        }
        for (const QString &message : messages)
        {
            QVERIFY(!message.contains(sentinelText));
        }
    }
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

void SshTerminalSessionTests::reportsAuthenticationRejectionOnRealHost()
{
    const QByteArray host = qgetenv("ZTERMY_TEST_SSH_HOST");
    const QByteArray privateKey = qgetenv("ZTERMY_TEST_SSH_PRIVATE_KEY");
    const QByteArray expectedFingerprint = qgetenv("ZTERMY_TEST_SSH_EXPECTED_FINGERPRINT");
    if (host.isEmpty() || privateKey.isEmpty() || expectedFingerprint.isEmpty())
    {
        QSKIP("Set the real-host private-key gate variables to run authentication rejection");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::ssh::SshTerminalSession session;
    QSignalSpy confirmationSpy(&session, &ztermy::ssh::SshTerminalSession::hostKeyConfirmationRequired);
    QSignalSpy failureSpy(&session, &ztermy::ssh::SshTerminalSession::failureOccurred);
    QSignalSpy statusSpy(&session, &ztermy::ssh::SshTerminalSession::statusChanged);
    ztermy::ssh::SshConnectionRequest request{
        .host = QString::fromUtf8(host),
        .port = 22,
        .username = QStringLiteral("ztermy-intentional-unknown-user"),
        .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
        .privateKeyPath = QString::fromUtf8(privateKey),
        .knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json")),
    };

    QVERIFY(!session.start(std::move(request), {.columns = 80, .rows = 24}));
    QTRY_COMPARE_WITH_TIMEOUT(confirmationSpy.count(), 1, 10s);
    QCOMPARE(confirmationSpy.constFirst().at(1).toString(), QString::fromLatin1(expectedFingerprint));
    session.confirmHostKey(true);
    QTRY_VERIFY_WITH_TIMEOUT(
        std::ranges::any_of(failureSpy,
                            [](const QList<QVariant> &arguments) {
                                return !arguments.isEmpty()
                                       && qvariant_cast<ztermy::ssh::SshFailureKind>(arguments.constFirst())
                                              == ztermy::ssh::SshFailureKind::AuthenticationRejected;
                            }),
        20s);
    QVERIFY(!statusSpy.isEmpty());
    QCOMPARE(statusSpy.constLast().constFirst().toString(),
             ztermy::ssh::sshFailureStatus(ztermy::ssh::SshFailureKind::AuthenticationRejected));
    session.stop();
}

void SshTerminalSessionTests::reportsRemoteCloseOnRealHost()
{
    const QByteArray host = qgetenv("ZTERMY_TEST_SSH_HOST");
    const QByteArray username = qgetenv("ZTERMY_TEST_SSH_USERNAME");
    const QByteArray privateKey = qgetenv("ZTERMY_TEST_SSH_PRIVATE_KEY");
    const QByteArray expectedFingerprint = qgetenv("ZTERMY_TEST_SSH_EXPECTED_FINGERPRINT");
    if (host.isEmpty() || username.isEmpty() || privateKey.isEmpty() || expectedFingerprint.isEmpty())
    {
        QSKIP("Set the real-host private-key gate variables to run remote close");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::ssh::SshTerminalSession session;
    QSignalSpy confirmationSpy(&session, &ztermy::ssh::SshTerminalSession::hostKeyConfirmationRequired);
    QSignalSpy runningSpy(&session, &ztermy::ssh::SshTerminalSession::runningChanged);
    QSignalSpy failureSpy(&session, &ztermy::ssh::SshTerminalSession::failureOccurred);
    QSignalSpy statusSpy(&session, &ztermy::ssh::SshTerminalSession::statusChanged);
    ztermy::ssh::SshConnectionRequest request{
        .host = QString::fromUtf8(host),
        .port = 22,
        .username = QString::fromUtf8(username),
        .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
        .privateKeyPath = QString::fromUtf8(privateKey),
        .knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json")),
    };

    QVERIFY(!session.start(std::move(request), {.columns = 80, .rows = 24}));
    QTRY_COMPARE_WITH_TIMEOUT(confirmationSpy.count(), 1, 10s);
    QCOMPARE(confirmationSpy.constFirst().at(1).toString(), QString::fromLatin1(expectedFingerprint));
    session.confirmHostKey(true);
    QTRY_VERIFY_WITH_TIMEOUT(std::ranges::any_of(runningSpy,
                                                 [](const QList<QVariant> &arguments) {
                                                     return !arguments.isEmpty() && arguments.constFirst().toBool();
                                                 }),
                             20s);

    session.queueInput(QByteArrayLiteral("exit\r"));
    QTRY_VERIFY_WITH_TIMEOUT(std::ranges::any_of(failureSpy,
                                                 [](const QList<QVariant> &arguments) {
                                                     return !arguments.isEmpty()
                                                            && qvariant_cast<ztermy::ssh::SshFailureKind>(
                                                                   arguments.constFirst())
                                                                   == ztermy::ssh::SshFailureKind::RemoteClosed;
                                                 }),
                             15s);
    QVERIFY(!statusSpy.isEmpty());
    QCOMPARE(statusSpy.constLast().constFirst().toString(),
             ztermy::ssh::sshFailureStatus(ztermy::ssh::SshFailureKind::RemoteClosed));
    session.stop();
}

void SshTerminalSessionTests::authenticatesWithInteractivePasswordOnRealHost()
{
    const bool invokedDirectly =
        QCoreApplication::arguments().contains(QStringLiteral("authenticatesWithInteractivePasswordOnRealHost"));
    if (!invokedDirectly || qgetenv("ZTERMY_TEST_SSH_PASSWORD_INTERACTIVE") != QByteArrayLiteral("1"))
    {
        QSKIP("Set ZTERMY_TEST_SSH_PASSWORD_INTERACTIVE=1 and invoke this test directly");
    }

    const QByteArray host = qgetenv("ZTERMY_TEST_SSH_HOST");
    const QByteArray username = qgetenv("ZTERMY_TEST_SSH_USERNAME");
    const QByteArray expectedFingerprint = qgetenv("ZTERMY_TEST_SSH_EXPECTED_FINGERPRINT");
    if (host.isEmpty() || username.isEmpty() || expectedFingerprint.isEmpty())
    {
        QSKIP("Set the real-host identity variables to run interactive password authentication");
    }

    auto password = readHiddenConsolePassword();
    if (!password)
    {
        QFAIL(qPrintable(password.error()));
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::ssh::SshTerminalSession session;
    QSignalSpy confirmationSpy(&session, &ztermy::ssh::SshTerminalSession::hostKeyConfirmationRequired);
    QSignalSpy runningSpy(&session, &ztermy::ssh::SshTerminalSession::runningChanged);
    ztermy::ssh::SshConnectionRequest request{
        .host = QString::fromUtf8(host),
        .port = 22,
        .username = QString::fromUtf8(username),
        .authentication = ztermy::ssh::SshAuthenticationMethod::Password,
        .secret = std::move(*password),
        .knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json")),
    };

    QVERIFY(!session.start(std::move(request), {.columns = 80, .rows = 24}));
    QTRY_COMPARE_WITH_TIMEOUT(confirmationSpy.count(), 1, 10s);
    QCOMPARE(confirmationSpy.constFirst().at(1).toString(), QString::fromLatin1(expectedFingerprint));
    session.confirmHostKey(true);
    QTRY_VERIFY_WITH_TIMEOUT(std::ranges::any_of(runningSpy,
                                                 [](const QList<QVariant> &arguments) {
                                                     return !arguments.isEmpty() && arguments.constFirst().toBool();
                                                 }),
                             20s);
    runningSpy.clear();
    session.stop();
    QTRY_VERIFY_WITH_TIMEOUT(std::ranges::any_of(runningSpy,
                                                 [](const QList<QVariant> &arguments) {
                                                     return !arguments.isEmpty() && !arguments.constFirst().toBool();
                                                 }),
                             5s);
}

void SshTerminalSessionTests::measuresInteractiveInputQueueLatency()
{
    if (qgetenv("ZTERMY_TEST_SSH_LATENCY") != QByteArrayLiteral("1"))
    {
        QSKIP("Set ZTERMY_TEST_SSH_LATENCY=1 to run the SSH input latency gate");
    }

    const QByteArray host = qgetenv("ZTERMY_TEST_SSH_HOST");
    const QByteArray username = qgetenv("ZTERMY_TEST_SSH_USERNAME");
    const QByteArray privateKey = qgetenv("ZTERMY_TEST_SSH_PRIVATE_KEY");
    const QByteArray expectedFingerprint = qgetenv("ZTERMY_TEST_SSH_EXPECTED_FINGERPRINT");
    if (host.isEmpty() || username.isEmpty() || privateKey.isEmpty() || expectedFingerprint.isEmpty())
    {
        QSKIP("Set the real-host private-key gate variables to run the SSH input latency gate");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    ztermy::ssh::SshTerminalSession session;
    QSignalSpy confirmationSpy(&session, &ztermy::ssh::SshTerminalSession::hostKeyConfirmationRequired);
    QSignalSpy runningSpy(&session, &ztermy::ssh::SshTerminalSession::runningChanged);
    const auto observedRunningState = [&runningSpy](const bool expected) {
        return std::ranges::any_of(runningSpy, [expected](const QList<QVariant> &arguments) {
            return !arguments.isEmpty() && arguments.constFirst().toBool() == expected;
        });
    };

    ztermy::ssh::SshConnectionRequest request{
        .host = QString::fromUtf8(host),
        .port = 22,
        .username = QString::fromUtf8(username),
        .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
        .privateKeyPath = QString::fromUtf8(privateKey),
        .knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json")),
    };
    QVERIFY(!session.start(std::move(request), {.columns = 80, .rows = 24}));
    QTRY_COMPARE_WITH_TIMEOUT(confirmationSpy.count(), 1, 10s);
    QCOMPARE(confirmationSpy.constFirst().at(1).toString(), QString::fromLatin1(expectedFingerprint));
    session.confirmHostKey(true);
    QTRY_VERIFY_WITH_TIMEOUT(observedRunningState(true), 15s);

    constexpr std::uint64_t sampleCount = 120;
    constexpr int simulatedKeyIntervalMilliseconds = 5;
    for (std::uint64_t sample = 0; sample < sampleCount; ++sample)
    {
        session.queueInput(QByteArrayLiteral(" "));
        QTest::qWait(simulatedKeyIntervalMilliseconds);
    }

    QTRY_VERIFY_WITH_TIMEOUT(session.inputQueueLatencySummary().count >= sampleCount, 5s);
    const ztermy::diagnostics::LatencySummary latency = session.inputQueueLatencySummary();
    QVERIFY2(latency.p95UpperBoundMicroseconds <= 16'000,
             qPrintable(QStringLiteral("SSH input queue P95 was %1 us across %2 samples")
                            .arg(latency.p95UpperBoundMicroseconds)
                            .arg(latency.count)));
    qInfo().noquote() << "SSH input latency gate:"
                      << "samples=" << latency.count << "p50Us=" << latency.p50UpperBoundMicroseconds
                      << "p95Us=" << latency.p95UpperBoundMicroseconds << "p99Us=" << latency.p99UpperBoundMicroseconds
                      << "maxUs=" << latency.maxMicroseconds;

    session.stop();
    QTRY_VERIFY_WITH_TIMEOUT(observedRunningState(false), 5s);
}

void SshTerminalSessionTests::survivesRepeatedConnectDisconnectCycles()
{
    if (qgetenv("ZTERMY_TEST_SSH_STRESS") != QByteArrayLiteral("1"))
    {
        QSKIP("Set ZTERMY_TEST_SSH_STRESS=1 to run the 20-cycle real-host gate");
    }

    const QByteArray host = qgetenv("ZTERMY_TEST_SSH_HOST");
    const QByteArray username = qgetenv("ZTERMY_TEST_SSH_USERNAME");
    const QByteArray privateKey = qgetenv("ZTERMY_TEST_SSH_PRIVATE_KEY");
    const QByteArray expectedFingerprint = qgetenv("ZTERMY_TEST_SSH_EXPECTED_FINGERPRINT");
    if (host.isEmpty() || username.isEmpty() || privateKey.isEmpty() || expectedFingerprint.isEmpty())
    {
        QSKIP("Set the real-host private-key gate variables to run the SSH lifecycle stress test");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    ztermy::ssh::SshTerminalSession session;
    QSignalSpy confirmationSpy(&session, &ztermy::ssh::SshTerminalSession::hostKeyConfirmationRequired);
    QSignalSpy runningSpy(&session, &ztermy::ssh::SshTerminalSession::runningChanged);
    QSignalSpy phaseSpy(&session, &ztermy::ssh::SshTerminalSession::phaseChanged);

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
    const auto observedRunningState = [](const QSignalSpy &spy, const bool expected) {
        return std::ranges::any_of(spy, [expected](const QList<QVariant> &arguments) {
            return !arguments.isEmpty() && arguments.constFirst().toBool() == expected;
        });
    };
    const auto observedPhase = [](const QSignalSpy &spy, const ztermy::ssh::SshConnectionPhase expected) {
        return std::ranges::any_of(spy, [expected](const QList<QVariant> &arguments) {
            return !arguments.isEmpty()
                   && qvariant_cast<ztermy::ssh::SshConnectionPhase>(arguments.constFirst()) == expected;
        });
    };

    QVERIFY(!session.start(request(), {.columns = 80, .rows = 24}));
    QTRY_COMPARE_WITH_TIMEOUT(confirmationSpy.count(), 1, 10s);
    QCOMPARE(confirmationSpy.constFirst().at(1).toString(), QString::fromLatin1(expectedFingerprint));
    session.confirmHostKey(true);
    QTRY_VERIFY_WITH_TIMEOUT(observedRunningState(runningSpy, true), 15s);
    session.stop();
    QTRY_VERIFY_WITH_TIMEOUT(observedRunningState(runningSpy, false), 5s);
    QTRY_VERIFY_WITH_TIMEOUT(observedPhase(phaseSpy, ztermy::ssh::SshConnectionPhase::Disconnected), 5s);

    DWORD baselineHandleCount = 0;
    QVERIFY(GetProcessHandleCount(GetCurrentProcess(), &baselineHandleCount));
    DWORD maximumHandleCount = baselineHandleCount;

    for (int cycle = 0; cycle < 20; ++cycle)
    {
        confirmationSpy.clear();
        runningSpy.clear();
        phaseSpy.clear();

        QVERIFY2(!session.start(request(), {.columns = 80, .rows = 24}),
                 qPrintable(QStringLiteral("SSH cycle %1 failed to start").arg(cycle + 1)));
        QTRY_VERIFY_WITH_TIMEOUT(observedRunningState(runningSpy, true), 15s);
        QCOMPARE(confirmationSpy.count(), 0);

        session.stop();
        QTRY_VERIFY_WITH_TIMEOUT(observedRunningState(runningSpy, false), 5s);
        QTRY_VERIFY_WITH_TIMEOUT(observedPhase(phaseSpy, ztermy::ssh::SshConnectionPhase::Disconnected), 5s);

        DWORD currentHandleCount = 0;
        QVERIFY(GetProcessHandleCount(GetCurrentProcess(), &currentHandleCount));
        maximumHandleCount = std::max(maximumHandleCount, currentHandleCount);
    }

    DWORD finalHandleCount = 0;
    QVERIFY(GetProcessHandleCount(GetCurrentProcess(), &finalHandleCount));
    qInfo().noquote() << "SSH lifecycle handle counts"
                      << "baseline=" << baselineHandleCount << "final=" << finalHandleCount
                      << "peak=" << maximumHandleCount;
    QVERIFY2(finalHandleCount <= baselineHandleCount + 4,
             qPrintable(QStringLiteral("Process handles grew from %1 to %2 (peak %3)")
                            .arg(baselineHandleCount)
                            .arg(finalHandleCount)
                            .arg(maximumHandleCount)));
}

QTEST_GUILESS_MAIN(SshTerminalSessionTests)

#include "ssh_terminal_session_tests.moc"
