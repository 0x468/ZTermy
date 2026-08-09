#include "application/ssh/SshConnectionBootstrap.h"

#include "domain/ssh/SshHostKey.h"
#include "infrastructure/ssh/KnownHostsStore.h"

#include <QByteArray>

#include <chrono>
#include <string>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace ztermy::ssh
{
namespace
{

class SecretClearGuard final
{
public:
    explicit SecretClearGuard(security::SensitiveByteArray &secret) noexcept : m_secret(secret) {}
    ~SecretClearGuard() { m_secret.clear(); }

    SecretClearGuard(const SecretClearGuard &) = delete;
    SecretClearGuard &operator=(const SecretClearGuard &) = delete;

private:
    security::SensitiveByteArray &m_secret;
};

[[nodiscard]] SshFailureKind failureFromTcp(const TcpConnectErrorKind kind) noexcept
{
    switch (kind)
    {
        case TcpConnectErrorKind::NameResolutionFailed:
            return SshFailureKind::NameResolutionFailed;
        case TcpConnectErrorKind::ConnectionRefused:
            return SshFailureKind::ConnectionRefused;
        case TcpConnectErrorKind::TimedOut:
            return SshFailureKind::TimedOut;
        case TcpConnectErrorKind::Cancelled:
            return SshFailureKind::Cancelled;
        case TcpConnectErrorKind::InvalidEndpoint:
        case TcpConnectErrorKind::NetworkUnreachable:
        case TcpConnectErrorKind::SystemError:
            return SshFailureKind::TransportError;
    }
    return SshFailureKind::TransportError;
}

void publishPhase(const SshConnectionCallbacks &callbacks, const SshConnectionPhase phase)
{
    if (callbacks.phaseChanged)
    {
        callbacks.phaseChanged(phase);
    }
}

[[nodiscard]] SshBootstrapError connectionError(const SshFailureKind failure) noexcept
{
    return SshBootstrapError{.failure = failure};
}

} // namespace

SshFailureKind sshFailureFromTransport(const SshTransportError &error, const bool openingChannel) noexcept
{
    switch (error.kind)
    {
        case SshTransportErrorKind::TimedOut:
            return SshFailureKind::TimedOut;
        case SshTransportErrorKind::Cancelled:
            return SshFailureKind::Cancelled;
        case SshTransportErrorKind::AuthenticationRejected:
            return SshFailureKind::AuthenticationRejected;
        case SshTransportErrorKind::AuthenticationUnavailable:
            return SshFailureKind::AuthenticationUnavailable;
        case SshTransportErrorKind::ConnectionLost:
            return SshFailureKind::RemoteClosed;
        case SshTransportErrorKind::InitializationFailed:
        case SshTransportErrorKind::InvalidArgument:
        case SshTransportErrorKind::InvalidState:
        case SshTransportErrorKind::ProtocolError:
            return openingChannel ? SshFailureKind::ChannelOpenFailed : SshFailureKind::ProtocolError;
    }
    return SshFailureKind::ProtocolError;
}

std::expected<AuthenticatedSshConnection, SshBootstrapError>
establishAuthenticatedSshConnection(SshConnectionRequest &request, const SshConnectionCallbacks &callbacks,
                                    const std::stop_token &stopToken)
{
    SecretClearGuard clearSecret(request.secret);
    if (!validSshConnectionRequest(request))
    {
        return std::unexpected(connectionError(SshFailureKind::ProtocolError));
    }

    publishPhase(callbacks, SshConnectionPhase::Connecting);
    const QByteArray hostUtf8 = request.host.trimmed().toUtf8();
    const std::string host(hostUtf8.constData(), static_cast<std::size_t>(hostUtf8.size()));
    auto socket = WindowsTcpSocket::connect(host, request.port, 10s, stopToken);
    if (!socket)
    {
        return std::unexpected(connectionError(failureFromTcp(socket.error().kind)));
    }

    auto session = Libssh2Session::create();
    if (!session)
    {
        return std::unexpected(connectionError(SshFailureKind::ProtocolError));
    }

    publishPhase(callbacks, SshConnectionPhase::Handshaking);
    auto handshake = (*session)->handshake(*socket, 10s, stopToken);
    if (!handshake)
    {
        return std::unexpected(connectionError(sshFailureFromTransport(handshake.error())));
    }

    publishPhase(callbacks, SshConnectionPhase::VerifyingHostKey);
    auto hostKey = (*session)->hostKey();
    const KnownHostsStore knownHostsStore(request.knownHostsPath);
    auto knownHosts = knownHostsStore.load();
    if (!hostKey || !knownHosts)
    {
        return std::unexpected(connectionError(SshFailureKind::HostKeyInvalid));
    }

    const SshEndpoint endpoint{.host = host, .port = request.port};
    auto trust = (*session)->verifyHostKey(endpoint, *knownHosts);
    if (!trust)
    {
        return std::unexpected(connectionError(SshFailureKind::HostKeyInvalid));
    }

    const QString algorithm = QString::fromUtf8(hostKeyAlgorithmName(hostKey->algorithm));
    const QString fingerprint = QString::fromStdString(sha256Fingerprint(*hostKey));
    if (*trust == HostKeyTrust::Changed)
    {
        if (callbacks.hostKeyChanged)
        {
            callbacks.hostKeyChanged(algorithm, fingerprint);
        }
        return std::unexpected(connectionError(SshFailureKind::HostKeyChanged));
    }

    if (*trust == HostKeyTrust::Unknown)
    {
        publishPhase(callbacks, SshConnectionPhase::AwaitingHostKeyConfirmation);
        const UnknownHostKeyDecision decision = callbacks.confirmUnknownHostKey
                                                    ? callbacks.confirmUnknownHostKey(algorithm, fingerprint)
                                                    : UnknownHostKeyDecision::Reject;
        if (decision == UnknownHostKeyDecision::Reject)
        {
            return std::unexpected(connectionError(stopToken.stop_requested() ? SshFailureKind::Cancelled
                                                                              : SshFailureKind::HostKeyInvalid));
        }

        knownHosts->push_back(KnownHostEntry{
            .endpoint = endpoint,
            .algorithm = hostKey->algorithm,
            .encodedKey = hostKey->encodedKey,
        });
        if (decision == UnknownHostKeyDecision::AcceptAndRemember && !knownHostsStore.save(*knownHosts))
        {
            return std::unexpected(SshBootstrapError{
                .failure = SshFailureKind::HostKeyInvalid,
                .reason = SshBootstrapErrorReason::KnownHostsSaveFailed,
            });
        }
        trust = (*session)->verifyHostKey(endpoint, *knownHosts);
        if (!trust || *trust != HostKeyTrust::Trusted)
        {
            return std::unexpected(connectionError(SshFailureKind::HostKeyInvalid));
        }
    }

    publishPhase(callbacks, SshConnectionPhase::Authenticating);
    const QByteArray usernameUtf8 = request.username.toUtf8();
    const QByteArray privateKeyPathUtf8 = request.privateKeyPath.toUtf8();
    const std::string username(usernameUtf8.constData(), static_cast<std::size_t>(usernameUtf8.size()));
    const std::string privateKeyPath(privateKeyPathUtf8.constData(),
                                     static_cast<std::size_t>(privateKeyPathUtf8.size()));
    std::expected<void, SshTransportError> authentication;
    switch (request.authentication)
    {
        case SshAuthenticationMethod::PrivateKey:
            authentication = (*session)->authenticateWithPrivateKeyFile(*socket, username, privateKeyPath,
                                                                        request.secret.view(), 15s, stopToken);
            break;
        case SshAuthenticationMethod::Password:
            authentication =
                (*session)->authenticateWithPassword(*socket, username, request.secret.view(), 15s, stopToken);
            break;
        case SshAuthenticationMethod::Agent:
            authentication = (*session)->authenticateWithAgent(*socket, username, 15s, stopToken);
            break;
    }
    if (!authentication)
    {
        return std::unexpected(connectionError(sshFailureFromTransport(authentication.error())));
    }

    return AuthenticatedSshConnection{
        .socket = std::move(*socket),
        .session = std::move(*session),
    };
}

} // namespace ztermy::ssh
