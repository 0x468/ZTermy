#include "infrastructure/ssh/Libssh2Session.h"
#include "infrastructure/ssh/WindowsTcpSocket.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTest>

#include <chrono>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace
{

struct SilentPeer final
{
    std::unique_ptr<QTcpServer> server;
    ztermy::ssh::WindowsTcpSocket socket;
};

[[nodiscard]] SilentPeer connectSilentPeer()
{
    auto server = std::make_unique<QTcpServer>();
    if (!server->listen(QHostAddress::LocalHost, 0))
    {
        return {};
    }

    auto socket = ztermy::ssh::WindowsTcpSocket::connect("127.0.0.1", server->serverPort(), 2s);
    if (!socket)
    {
        return {};
    }

    return {
        .server = std::move(server),
        .socket = std::move(*socket),
    };
}

} // namespace

class SshHandshakeTests final : public QObject
{
    Q_OBJECT

private slots:
    void createsNonBlockingSession();
    void timesOutAgainstSilentPeer();
    void honorsPreRequestedCancellation();
    void cancelsBlockedHandshake();
    void rejectsInvalidSocket();
    void rejectsHostKeyBeforeHandshake();
    void rejectsInvalidPasswordCredentials();
    void rejectsPasswordAuthenticationBeforeHandshake();
    void rejectsInvalidPrivateKeyCredentials();
    void rejectsPrivateKeyAuthenticationBeforeHandshake();
};

void SshHandshakeTests::createsNonBlockingSession()
{
    auto session = ztermy::ssh::Libssh2Session::create();
    if (!session)
    {
        QFAIL("libssh2 session creation failed");
    }
    QVERIFY(!(*session)->handshakeComplete());
}

void SshHandshakeTests::timesOutAgainstSilentPeer()
{
    SilentPeer peer = connectSilentPeer();
    QVERIFY(peer.server);
    QVERIFY(peer.server->isListening());
    QVERIFY(peer.socket.valid());

    auto session = ztermy::ssh::Libssh2Session::create();
    if (!session)
    {
        QFAIL("libssh2 session creation failed");
    }

    auto result = (*session)->handshake(peer.socket, 100ms);
    QVERIFY(!result);
    QCOMPARE(result.error().kind, ztermy::ssh::SshTransportErrorKind::TimedOut);
    QVERIFY(!(*session)->handshakeComplete());
}

void SshHandshakeTests::honorsPreRequestedCancellation()
{
    SilentPeer peer = connectSilentPeer();
    QVERIFY(peer.server);
    QVERIFY(peer.server->isListening());
    QVERIFY(peer.socket.valid());

    auto session = ztermy::ssh::Libssh2Session::create();
    if (!session)
    {
        QFAIL("libssh2 session creation failed");
    }

    std::stop_source stopSource;
    stopSource.request_stop();
    auto result = (*session)->handshake(peer.socket, 2s, stopSource.get_token());
    QVERIFY(!result);
    QCOMPARE(result.error().kind, ztermy::ssh::SshTransportErrorKind::Cancelled);
}

void SshHandshakeTests::cancelsBlockedHandshake()
{
    SilentPeer peer = connectSilentPeer();
    QVERIFY(peer.server);
    QVERIFY(peer.server->isListening());
    QVERIFY(peer.socket.valid());

    auto session = ztermy::ssh::Libssh2Session::create();
    if (!session)
    {
        QFAIL("libssh2 session creation failed");
    }

    std::stop_source stopSource;
    std::jthread canceller([&stopSource] {
        std::this_thread::sleep_for(50ms);
        stopSource.request_stop();
    });

    const auto startedAt = std::chrono::steady_clock::now();
    auto result = (*session)->handshake(peer.socket, 2s, stopSource.get_token());
    const auto elapsed = std::chrono::steady_clock::now() - startedAt;

    QVERIFY(!result);
    QCOMPARE(result.error().kind, ztermy::ssh::SshTransportErrorKind::Cancelled);
    QVERIFY(elapsed < 500ms);
}

void SshHandshakeTests::rejectsInvalidSocket()
{
    ztermy::ssh::WindowsTcpSocket socket;
    auto session = ztermy::ssh::Libssh2Session::create();
    if (!session)
    {
        QFAIL("libssh2 session creation failed");
    }

    auto result = (*session)->handshake(socket, 2s);
    QVERIFY(!result);
    QCOMPARE(result.error().kind, ztermy::ssh::SshTransportErrorKind::InvalidState);
}

void SshHandshakeTests::rejectsHostKeyBeforeHandshake()
{
    auto session = ztermy::ssh::Libssh2Session::create();
    if (!session)
    {
        QFAIL("libssh2 session creation failed");
    }

    auto hostKey = (*session)->hostKey();
    QVERIFY(!hostKey);
    QCOMPARE(hostKey.error().kind, ztermy::ssh::SshTransportErrorKind::InvalidState);
}

void SshHandshakeTests::rejectsInvalidPasswordCredentials()
{
    ztermy::ssh::WindowsTcpSocket socket;
    auto session = ztermy::ssh::Libssh2Session::create();
    if (!session)
    {
        QFAIL("libssh2 session creation failed");
    }

    auto emptyUsername = (*session)->authenticateWithPassword(socket, {}, "password", 2s);
    QVERIFY(!emptyUsername);
    QCOMPARE(emptyUsername.error().kind, ztermy::ssh::SshTransportErrorKind::InvalidArgument);

    auto emptyPassword = (*session)->authenticateWithPassword(socket, "user", {}, 2s);
    QVERIFY(!emptyPassword);
    QCOMPARE(emptyPassword.error().kind, ztermy::ssh::SshTransportErrorKind::InvalidArgument);
}

void SshHandshakeTests::rejectsPasswordAuthenticationBeforeHandshake()
{
    SilentPeer peer = connectSilentPeer();
    QVERIFY(peer.socket.valid());

    auto session = ztermy::ssh::Libssh2Session::create();
    if (!session)
    {
        QFAIL("libssh2 session creation failed");
    }

    auto result = (*session)->authenticateWithPassword(peer.socket, "user", "password", 2s);
    QVERIFY(!result);
    QCOMPARE(result.error().kind, ztermy::ssh::SshTransportErrorKind::InvalidState);
    QVERIFY(!(*session)->authenticated());
}

void SshHandshakeTests::rejectsInvalidPrivateKeyCredentials()
{
    ztermy::ssh::WindowsTcpSocket socket;
    auto session = ztermy::ssh::Libssh2Session::create();
    if (!session)
    {
        QFAIL("libssh2 session creation failed");
    }

    auto emptyUsername = (*session)->authenticateWithPrivateKeyFile(socket, {}, "key", {}, 2s);
    QVERIFY(!emptyUsername);
    QCOMPARE(emptyUsername.error().kind, ztermy::ssh::SshTransportErrorKind::InvalidArgument);

    auto emptyPath = (*session)->authenticateWithPrivateKeyFile(socket, "user", {}, {}, 2s);
    QVERIFY(!emptyPath);
    QCOMPARE(emptyPath.error().kind, ztermy::ssh::SshTransportErrorKind::InvalidArgument);

    const std::string embeddedNullPath("key\0path", 8);
    auto invalidPath = (*session)->authenticateWithPrivateKeyFile(socket, "user", embeddedNullPath, {}, 2s);
    QVERIFY(!invalidPath);
    QCOMPARE(invalidPath.error().kind, ztermy::ssh::SshTransportErrorKind::InvalidArgument);

    const std::string embeddedNullPassphrase("pass\0phrase", 11);
    auto invalidPassphrase =
        (*session)->authenticateWithPrivateKeyFile(socket, "user", "key", embeddedNullPassphrase, 2s);
    QVERIFY(!invalidPassphrase);
    QCOMPARE(invalidPassphrase.error().kind, ztermy::ssh::SshTransportErrorKind::InvalidArgument);
}

void SshHandshakeTests::rejectsPrivateKeyAuthenticationBeforeHandshake()
{
    SilentPeer peer = connectSilentPeer();
    QVERIFY(peer.socket.valid());

    auto session = ztermy::ssh::Libssh2Session::create();
    if (!session)
    {
        QFAIL("libssh2 session creation failed");
    }

    auto result = (*session)->authenticateWithPrivateKeyFile(peer.socket, "user", "key", {}, 2s);
    QVERIFY(!result);
    QCOMPARE(result.error().kind, ztermy::ssh::SshTransportErrorKind::InvalidState);
    QVERIFY(!(*session)->authenticated());
}

QTEST_GUILESS_MAIN(SshHandshakeTests)

#include "ssh_handshake_tests.moc"
