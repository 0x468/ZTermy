#include "domain/ssh/SshHostKey.h"
#include "infrastructure/ssh/Libssh2Session.h"
#include "infrastructure/ssh/WindowsTcpSocket.h"

#include <QByteArray>
#include <QTest>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

class SshRealHostTests final : public QObject
{
    Q_OBJECT

private slots:
    void observesUnknownHostBeforeAuthentication();
    void authenticatesWithExplicitPrivateKey();
    void opensAndClosesTerminalWithExplicitPrivateKey();
};

namespace
{

[[nodiscard]] std::uint16_t configuredPort()
{
    bool portValid = false;
    const int port = qEnvironmentVariableIntValue("ZTERMY_TEST_SSH_PORT", &portValid);
    return portValid && port > 0 && port <= 65535 ? static_cast<std::uint16_t>(port) : 22;
}

} // namespace

void SshRealHostTests::observesUnknownHostBeforeAuthentication()
{
    const QByteArray hostValue = qgetenv("ZTERMY_TEST_SSH_HOST");
    if (hostValue.isEmpty())
    {
        QSKIP("Set ZTERMY_TEST_SSH_HOST to run the real-host gate");
    }

    const std::uint16_t port = configuredPort();
    const std::string host(hostValue.constData(), static_cast<std::size_t>(hostValue.size()));

    auto socket = ztermy::ssh::WindowsTcpSocket::connect(host, port, 5s);
    if (!socket)
    {
        QFAIL(qPrintable(QStringLiteral("TCP connection failed: kind=%1 native=%2")
                             .arg(static_cast<int>(socket.error().kind))
                             .arg(socket.error().nativeCode)));
    }

    auto session = ztermy::ssh::Libssh2Session::create();
    if (!session)
    {
        QFAIL("libssh2 session creation failed");
    }

    auto handshake = (*session)->handshake(*socket, 5s);
    if (!handshake)
    {
        QFAIL(qPrintable(QStringLiteral("SSH handshake failed: kind=%1 libssh2=%2 native=%3")
                             .arg(static_cast<int>(handshake.error().kind))
                             .arg(handshake.error().libssh2Code)
                             .arg(handshake.error().nativeCode)));
    }

    auto hostKey = (*session)->hostKey();
    if (!hostKey)
    {
        QFAIL("server host key extraction failed");
    }

    const ztermy::ssh::SshEndpoint endpoint{.host = host, .port = port};
    auto unknownTrust = (*session)->verifyHostKey(endpoint, {});
    QVERIFY(unknownTrust);
    QCOMPARE(*unknownTrust, ztermy::ssh::HostKeyTrust::Unknown);

    auto blockedAuthentication = (*session)->authenticateWithPassword(*socket, "unused", "unused", 1s);
    QVERIFY(!blockedAuthentication);
    QCOMPARE(blockedAuthentication.error().kind, ztermy::ssh::SshTransportErrorKind::InvalidState);

    auto changedEncodedKey = hostKey->encodedKey;
    QVERIFY(!changedEncodedKey.empty());
    changedEncodedKey.front() ^= std::uint8_t{1};
    const std::vector changedKnownHosts{ztermy::ssh::KnownHostEntry{
        .endpoint = endpoint,
        .algorithm = hostKey->algorithm,
        .encodedKey = std::move(changedEncodedKey),
    }};
    auto changedTrust = (*session)->verifyHostKey(endpoint, changedKnownHosts);
    QVERIFY(changedTrust);
    QCOMPARE(*changedTrust, ztermy::ssh::HostKeyTrust::Changed);

    blockedAuthentication = (*session)->authenticateWithPassword(*socket, "unused", "unused", 1s);
    QVERIFY(!blockedAuthentication);
    QCOMPARE(blockedAuthentication.error().kind, ztermy::ssh::SshTransportErrorKind::InvalidState);

    const std::vector knownHosts{ztermy::ssh::KnownHostEntry{
        .endpoint = endpoint,
        .algorithm = hostKey->algorithm,
        .encodedKey = hostKey->encodedKey,
    }};
    auto trusted = (*session)->verifyHostKey(endpoint, knownHosts);
    QVERIFY(trusted);
    QCOMPARE(*trusted, ztermy::ssh::HostKeyTrust::Trusted);

    auto zeroTimeout = (*session)->authenticateWithPassword(*socket, "unused", "unused", 0ms);
    QVERIFY(!zeroTimeout);
    QCOMPARE(zeroTimeout.error().kind, ztermy::ssh::SshTransportErrorKind::TimedOut);
    QVERIFY(!(*session)->authenticated());

    qInfo().noquote() << "Observed host key:"
                      << QString::fromUtf8(ztermy::ssh::hostKeyAlgorithmName(hostKey->algorithm))
                      << QString::fromStdString(ztermy::ssh::sha256Fingerprint(*hostKey));
}

void SshRealHostTests::authenticatesWithExplicitPrivateKey()
{
    const QByteArray hostValue = qgetenv("ZTERMY_TEST_SSH_HOST");
    const QByteArray usernameValue = qgetenv("ZTERMY_TEST_SSH_USERNAME");
    const QByteArray privateKeyValue = qgetenv("ZTERMY_TEST_SSH_PRIVATE_KEY");
    const QByteArray expectedFingerprint = qgetenv("ZTERMY_TEST_SSH_EXPECTED_FINGERPRINT");
    if (hostValue.isEmpty() || usernameValue.isEmpty() || privateKeyValue.isEmpty() || expectedFingerprint.isEmpty())
    {
        QSKIP("Set the host, username, private-key path, and expected fingerprint to run private-key auth");
    }

    const std::uint16_t port = configuredPort();
    const std::string host(hostValue.constData(), static_cast<std::size_t>(hostValue.size()));
    auto socket = ztermy::ssh::WindowsTcpSocket::connect(host, port, 5s);
    if (!socket)
    {
        QFAIL("TCP connection failed");
    }

    auto session = ztermy::ssh::Libssh2Session::create();
    if (!session)
    {
        QFAIL("libssh2 session creation failed");
    }
    auto handshake = (*session)->handshake(*socket, 5s);
    if (!handshake)
    {
        QFAIL("SSH handshake failed");
    }

    auto hostKey = (*session)->hostKey();
    if (!hostKey)
    {
        QFAIL("server host key extraction failed");
    }
    const QByteArray actualFingerprint = QByteArray::fromStdString(ztermy::ssh::sha256Fingerprint(*hostKey));
    if (actualFingerprint != expectedFingerprint)
    {
        QFAIL(qPrintable(
            QStringLiteral("Host fingerprint mismatch; observed %1").arg(QString::fromLatin1(actualFingerprint))));
    }

    const ztermy::ssh::SshEndpoint endpoint{.host = host, .port = port};
    const std::vector knownHosts{ztermy::ssh::KnownHostEntry{
        .endpoint = endpoint,
        .algorithm = hostKey->algorithm,
        .encodedKey = hostKey->encodedKey,
    }};
    auto trust = (*session)->verifyHostKey(endpoint, knownHosts);
    QVERIFY(trust);
    QCOMPARE(*trust, ztermy::ssh::HostKeyTrust::Trusted);

    const std::string username(usernameValue.constData(), static_cast<std::size_t>(usernameValue.size()));
    const std::string privateKey(privateKeyValue.constData(), static_cast<std::size_t>(privateKeyValue.size()));
    auto authentication = (*session)->authenticateWithPrivateKeyFile(*socket, username, privateKey, {}, 10s);
    if (!authentication)
    {
        QFAIL(qPrintable(QStringLiteral("Private-key authentication failed: kind=%1 libssh2=%2")
                             .arg(static_cast<int>(authentication.error().kind))
                             .arg(authentication.error().libssh2Code)));
    }
    QVERIFY((*session)->authenticated());
    qInfo("Private-key authentication succeeded");
}

void SshRealHostTests::opensAndClosesTerminalWithExplicitPrivateKey()
{
    const QByteArray hostValue = qgetenv("ZTERMY_TEST_SSH_HOST");
    const QByteArray usernameValue = qgetenv("ZTERMY_TEST_SSH_USERNAME");
    const QByteArray privateKeyValue = qgetenv("ZTERMY_TEST_SSH_PRIVATE_KEY");
    const QByteArray expectedFingerprint = qgetenv("ZTERMY_TEST_SSH_EXPECTED_FINGERPRINT");
    if (hostValue.isEmpty() || usernameValue.isEmpty() || privateKeyValue.isEmpty() || expectedFingerprint.isEmpty())
    {
        QSKIP("Set the host, username, private-key path, and expected fingerprint to run terminal channel gate");
    }

    const std::uint16_t port = configuredPort();
    const std::string host(hostValue.constData(), static_cast<std::size_t>(hostValue.size()));
    auto socket = ztermy::ssh::WindowsTcpSocket::connect(host, port, 5s);
    QVERIFY(socket);

    auto session = ztermy::ssh::Libssh2Session::create();
    QVERIFY(session);
    QVERIFY((*session)->handshake(*socket, 5s));

    auto hostKey = (*session)->hostKey();
    QVERIFY(hostKey);
    const QByteArray actualFingerprint = QByteArray::fromStdString(ztermy::ssh::sha256Fingerprint(*hostKey));
    if (actualFingerprint != expectedFingerprint)
    {
        QFAIL(qPrintable(
            QStringLiteral("Host fingerprint mismatch; observed %1").arg(QString::fromLatin1(actualFingerprint))));
    }

    const ztermy::ssh::SshEndpoint endpoint{.host = host, .port = port};
    const std::vector knownHosts{ztermy::ssh::KnownHostEntry{
        .endpoint = endpoint,
        .algorithm = hostKey->algorithm,
        .encodedKey = hostKey->encodedKey,
    }};
    auto trust = (*session)->verifyHostKey(endpoint, knownHosts);
    QVERIFY(trust);
    QCOMPARE(*trust, ztermy::ssh::HostKeyTrust::Trusted);

    const std::string username(usernameValue.constData(), static_cast<std::size_t>(usernameValue.size()));
    const std::string privateKey(privateKeyValue.constData(), static_cast<std::size_t>(privateKeyValue.size()));
    QVERIFY((*session)->authenticateWithPrivateKeyFile(*socket, username, privateKey, {}, 10s));

    auto open = (*session)->openTerminal(*socket, 80, 24, "xterm-256color", 10s);
    QVERIFY(open);
    QVERIFY((*session)->terminalOpen());

    auto resize = (*session)->resizeTerminal(*socket, 100, 30, 5s);
    QVERIFY(resize);

    auto close = (*session)->closeTerminal(*socket, 5s);
    QVERIFY(close);
    QVERIFY(!(*session)->terminalOpen());
    qInfo("SSH terminal channel opened, resized, and closed");
}

QTEST_GUILESS_MAIN(SshRealHostTests)

#include "ssh_real_host_tests.moc"
