#include "application/ssh/SshConnectionBootstrap.h"
#include "application/ssh/SshConnectionRequest.h"

#include <QByteArray>
#include <QtTest/QTest>

namespace
{

class SshConnectionBootstrapTests final : public QObject
{
    Q_OBJECT

private slots:
    void validatesReusableConnectionRequests();
    void clearsSecretsWhenBootstrapRejectsARequest();
    void mapsTransportFailuresForConnectionAndChannelStages();
};

ztermy::ssh::SshConnectionRequest validPasswordRequest()
{
    return ztermy::ssh::SshConnectionRequest{
        .host = QStringLiteral("example.test"),
        .port = 22,
        .username = QStringLiteral("tester"),
        .authentication = ztermy::ssh::SshAuthenticationMethod::Password,
        .secret = ztermy::security::SensitiveByteArray(QByteArray("secret")),
        .knownHostsPath = QStringLiteral("known_hosts"),
    };
}

void SshConnectionBootstrapTests::validatesReusableConnectionRequests()
{
    auto password = validPasswordRequest();
    QVERIFY(ztermy::ssh::validSshConnectionRequest(password));

    ztermy::ssh::SshConnectionRequest privateKey{
        .host = QStringLiteral("example.test"),
        .port = 2222,
        .username = QStringLiteral("tester"),
        .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
        .privateKeyPath = QStringLiteral("id_ed25519"),
        .knownHostsPath = QStringLiteral("known_hosts"),
    };
    QVERIFY(ztermy::ssh::validSshConnectionRequest(privateKey));

    ztermy::ssh::SshConnectionRequest agent{
        .host = QStringLiteral("example.test"),
        .port = 22,
        .username = QStringLiteral("tester"),
        .authentication = ztermy::ssh::SshAuthenticationMethod::Agent,
        .knownHostsPath = QStringLiteral("known_hosts"),
    };
    QVERIFY(ztermy::ssh::validSshConnectionRequest(agent));

    agent.privateKeyPath = QStringLiteral("id_ed25519");
    QVERIFY(!ztermy::ssh::validSshConnectionRequest(agent));
    agent.privateKeyPath.clear();
    agent.secret = ztermy::security::SensitiveByteArray(QByteArray("must-not-enter-agent-flow"));
    QVERIFY(!ztermy::ssh::validSshConnectionRequest(agent));

    password.host.clear();
    QVERIFY(!ztermy::ssh::validSshConnectionRequest(password));
    privateKey.privateKeyPath.clear();
    QVERIFY(!ztermy::ssh::validSshConnectionRequest(privateKey));

    auto invalidSessionOptions = validPasswordRequest();
    invalidSessionOptions.sessionOptions.terminalType = "xterm 256color";
    QVERIFY(!ztermy::ssh::validSshConnectionRequest(invalidSessionOptions));
    invalidSessionOptions = validPasswordRequest();
    invalidSessionOptions.sessionOptions.connectionTimeoutSeconds = 0;
    QVERIFY(!ztermy::ssh::validSshConnectionRequest(invalidSessionOptions));
    invalidSessionOptions = validPasswordRequest();
    invalidSessionOptions.sessionOptions.authenticationTimeoutSeconds = 0;
    QVERIFY(!ztermy::ssh::validSshConnectionRequest(invalidSessionOptions));
    invalidSessionOptions = validPasswordRequest();
    invalidSessionOptions.sessionOptions.terminalOpenTimeoutSeconds = 301;
    QVERIFY(!ztermy::ssh::validSshConnectionRequest(invalidSessionOptions));

    auto proxied = validPasswordRequest();
    proxied.proxy = {.type = ztermy::ssh::SshProxyType::HttpConnect,
                     .host = "proxy.example.test",
                     .port = 8080,
                     .username = "proxy-user",
                     .credentialReference = "proxy-profile"};
    QVERIFY(!ztermy::ssh::validSshConnectionRequest(proxied));
    proxied.proxySecret = ztermy::security::SensitiveByteArray(QByteArray("proxy-secret"));
    QVERIFY(ztermy::ssh::validSshConnectionRequest(proxied));

    auto anonymousProxy = validPasswordRequest();
    anonymousProxy.proxy = {.type = ztermy::ssh::SshProxyType::Socks5, .host = "proxy.example.test", .port = 1080};
    QVERIFY(ztermy::ssh::validSshConnectionRequest(anonymousProxy));
    anonymousProxy.proxySecret = ztermy::security::SensitiveByteArray(QByteArray("unexpected"));
    QVERIFY(!ztermy::ssh::validSshConnectionRequest(anonymousProxy));

    auto jumped = validPasswordRequest();
    jumped.jumpHosts.push_back({
        .profileId = QStringLiteral("jump-1"),
        .displayName = QStringLiteral("Jump one"),
        .host = QStringLiteral("jump.example.test"),
        .port = 22,
        .username = QStringLiteral("jump-user"),
        .authentication = ztermy::ssh::SshAuthenticationMethod::Password,
        .secret = ztermy::security::SensitiveByteArray(QByteArray("jump-secret")),
    });
    QVERIFY(ztermy::ssh::validSshConnectionRequest(jumped));
    jumped.jumpHosts.front().connectionTimeoutSeconds = 0;
    QVERIFY(!ztermy::ssh::validSshConnectionRequest(jumped));
    jumped.jumpHosts.front().connectionTimeoutSeconds = 10;
    jumped.jumpHosts.front().authenticationTimeoutSeconds = 0;
    QVERIFY(!ztermy::ssh::validSshConnectionRequest(jumped));
    jumped.jumpHosts.front().authenticationTimeoutSeconds = 30;
    jumped.jumpHosts.push_back({
        .profileId = QStringLiteral("jump-1"),
        .displayName = QStringLiteral("Duplicate jump"),
        .host = QStringLiteral("duplicate.example.test"),
        .port = 22,
        .username = QStringLiteral("jump-user"),
        .authentication = ztermy::ssh::SshAuthenticationMethod::Agent,
    });
    QVERIFY(!ztermy::ssh::validSshConnectionRequest(jumped));
}

void SshConnectionBootstrapTests::clearsSecretsWhenBootstrapRejectsARequest()
{
    auto request = validPasswordRequest();
    request.host.clear();
    request.proxySecret = ztermy::security::SensitiveByteArray(QByteArray("proxy-secret"));
    request.jumpHosts.push_back({
        .profileId = QStringLiteral("jump-1"),
        .displayName = QStringLiteral("Jump one"),
        .host = QStringLiteral("jump.example.test"),
        .port = 22,
        .username = QStringLiteral("jump-user"),
        .authentication = ztermy::ssh::SshAuthenticationMethod::Password,
        .secret = ztermy::security::SensitiveByteArray(QByteArray("jump-secret")),
        .proxy = {.type = ztermy::ssh::SshProxyType::HttpConnect,
                  .host = "proxy.example.test",
                  .port = 8080,
                  .username = "proxy-user"},
        .proxySecret = ztermy::security::SensitiveByteArray(QByteArray("jump-proxy-secret")),
    });
    QVERIFY(!request.secret.empty());
    QVERIFY(!request.proxySecret.empty());

    const auto result = ztermy::ssh::establishAuthenticatedSshConnection(request, {});
    QVERIFY(!result);
    QCOMPARE(result.error().failure, ztermy::ssh::SshFailureKind::ProtocolError);
    QVERIFY(request.secret.empty());
    QVERIFY(request.proxySecret.empty());
    QVERIFY(request.jumpHosts.front().secret.empty());
    QVERIFY(request.jumpHosts.front().proxySecret.empty());
}

void SshConnectionBootstrapTests::mapsTransportFailuresForConnectionAndChannelStages()
{
    const ztermy::ssh::SshTransportError timeout{.kind = ztermy::ssh::SshTransportErrorKind::TimedOut};
    QCOMPARE(ztermy::ssh::sshFailureFromTransport(timeout), ztermy::ssh::SshFailureKind::TimedOut);

    const ztermy::ssh::SshTransportError protocol{.kind = ztermy::ssh::SshTransportErrorKind::ProtocolError};
    QCOMPARE(ztermy::ssh::sshFailureFromTransport(protocol), ztermy::ssh::SshFailureKind::ProtocolError);
    QCOMPARE(ztermy::ssh::sshFailureFromTransport(protocol, true), ztermy::ssh::SshFailureKind::ChannelOpenFailed);
}

} // namespace

QTEST_GUILESS_MAIN(SshConnectionBootstrapTests)

#include "ssh_connection_bootstrap_tests.moc"
