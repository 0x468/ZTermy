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

    password.host.clear();
    QVERIFY(!ztermy::ssh::validSshConnectionRequest(password));
    privateKey.privateKeyPath.clear();
    QVERIFY(!ztermy::ssh::validSshConnectionRequest(privateKey));
}

void SshConnectionBootstrapTests::clearsSecretsWhenBootstrapRejectsARequest()
{
    auto request = validPasswordRequest();
    request.host.clear();
    QVERIFY(!request.secret.empty());

    const auto result = ztermy::ssh::establishAuthenticatedSshConnection(request, {});
    QVERIFY(!result);
    QCOMPARE(result.error().failure, ztermy::ssh::SshFailureKind::ProtocolError);
    QVERIFY(request.secret.empty());
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
