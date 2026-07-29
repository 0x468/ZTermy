#include "domain/ssh/SshHostKey.h"
#include "infrastructure/ssh/Libssh2Session.h"
#include "infrastructure/ssh/WindowsTcpSocket.h"

#include <QByteArray>
#include <QTest>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

using namespace std::chrono_literals;

class SshRealHostTests final : public QObject
{
    Q_OBJECT

private slots:
    void observesUnknownHostBeforeAuthentication();
};

void SshRealHostTests::observesUnknownHostBeforeAuthentication()
{
    const QByteArray hostValue = qgetenv("ZTERMY_TEST_SSH_HOST");
    if (hostValue.isEmpty())
    {
        QSKIP("Set ZTERMY_TEST_SSH_HOST to run the real-host gate");
    }

    bool portValid = false;
    const int configuredPort = qEnvironmentVariableIntValue("ZTERMY_TEST_SSH_PORT", &portValid);
    const std::uint16_t port =
        portValid && configuredPort > 0 && configuredPort <= 65535 ? static_cast<std::uint16_t>(configuredPort) : 22;
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

QTEST_GUILESS_MAIN(SshRealHostTests)

#include "ssh_real_host_tests.moc"
