#include "application/ssh/SshConnectionBootstrap.h"

#include "domain/ssh/SshHostKey.h"
#include "infrastructure/ssh/ExplicitProxyTunnel.h"
#include "infrastructure/ssh/KnownHostsStore.h"
#include "infrastructure/ssh/SshDirectTcpipTransport.h"
#include "infrastructure/ssh/WindowsTcpSocket.h"

#include <QByteArray>

#include <chrono>
#include <new>
#include <string>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace ztermy::ssh
{
namespace
{

class RequestSecretsClearGuard final
{
public:
    explicit RequestSecretsClearGuard(SshConnectionRequest &request) noexcept : m_request(request) {}
    ~RequestSecretsClearGuard()
    {
        m_request.secret.clear();
        m_request.proxySecret.clear();
        for (SshJumpHostRequest &jump : m_request.jumpHosts)
        {
            jump.secret.clear();
            jump.proxySecret.clear();
        }
    }

    RequestSecretsClearGuard(const RequestSecretsClearGuard &) = delete;
    RequestSecretsClearGuard &operator=(const RequestSecretsClearGuard &) = delete;

private:
    SshConnectionRequest &m_request;
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

[[nodiscard]] SshFailureKind failureFromProxy(const ExplicitProxyErrorKind kind) noexcept
{
    switch (kind)
    {
        case ExplicitProxyErrorKind::AuthenticationRequired:
        case ExplicitProxyErrorKind::AuthenticationRejected:
            return SshFailureKind::AuthenticationRejected;
        case ExplicitProxyErrorKind::TimedOut:
            return SshFailureKind::TimedOut;
        case ExplicitProxyErrorKind::Cancelled:
            return SshFailureKind::Cancelled;
        case ExplicitProxyErrorKind::ConnectionLost:
        case ExplicitProxyErrorKind::ConnectionRejected:
            return SshFailureKind::TransportError;
        case ExplicitProxyErrorKind::InvalidConfiguration:
        case ExplicitProxyErrorKind::ProtocolError:
            return SshFailureKind::ProtocolError;
    }
    return SshFailureKind::ProtocolError;
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

template <typename Endpoint>
[[nodiscard]] std::string endpointHost(const Endpoint &endpoint)
{
    const QByteArray hostUtf8 = endpoint.host.trimmed().toUtf8();
    return {hostUtf8.constData(), static_cast<std::size_t>(hostUtf8.size())};
}

template <typename Endpoint>
[[nodiscard]] QString endpointDescription(const Endpoint &endpoint)
{
    QString address = QStringLiteral("%1@%2:%3").arg(endpoint.username, endpoint.host).arg(endpoint.port);
    if constexpr (requires { endpoint.displayName; })
    {
        return endpoint.displayName.isEmpty() ? address : QStringLiteral("%1 — %2").arg(endpoint.displayName, address);
    }
    return address;
}

template <typename Endpoint>
[[nodiscard]] std::chrono::seconds endpointConnectionTimeout(const Endpoint &endpoint) noexcept
{
    if constexpr (requires { endpoint.connectionTimeoutSeconds; })
    {
        return std::chrono::seconds(endpoint.connectionTimeoutSeconds);
    }
    else
    {
        return std::chrono::seconds(endpoint.sessionOptions.connectionTimeoutSeconds);
    }
}

template <typename Endpoint>
[[nodiscard]] std::chrono::seconds endpointAuthenticationTimeout(const Endpoint &endpoint) noexcept
{
    if constexpr (requires { endpoint.authenticationTimeoutSeconds; })
    {
        return std::chrono::seconds(endpoint.authenticationTimeoutSeconds);
    }
    else
    {
        return std::chrono::seconds(endpoint.sessionOptions.authenticationTimeoutSeconds);
    }
}

template <typename Endpoint>
[[nodiscard]] std::expected<std::unique_ptr<SshByteTransport>, SshBootstrapError>
connectInitialTransport(Endpoint &endpoint, const std::stop_token &stopToken)
{
    std::string host;
    try
    {
        host = endpointHost(endpoint);
    }
    catch (const std::bad_alloc &)
    {
        return std::unexpected(connectionError(SshFailureKind::ProtocolError));
    }
    const std::string &connectHost = endpoint.proxy.type == SshProxyType::None ? host : endpoint.proxy.host;
    const std::uint16_t connectPort = endpoint.proxy.type == SshProxyType::None ? endpoint.port : endpoint.proxy.port;
    const auto timeout = endpointConnectionTimeout(endpoint);
    auto directSocket = WindowsTcpSocket::connect(connectHost, connectPort, timeout, stopToken);
    if (!directSocket)
    {
        return std::unexpected(connectionError(failureFromTcp(directSocket.error().kind)));
    }

    std::unique_ptr<SshByteTransport> transport;
    try
    {
        transport = std::make_unique<WindowsTcpSocket>(std::move(*directSocket));
    }
    catch (const std::bad_alloc &)
    {
        return std::unexpected(connectionError(SshFailureKind::ProtocolError));
    }
    if (endpoint.proxy.type != SshProxyType::None)
    {
        auto tunnel =
            establishExplicitProxyTunnel(*transport, endpoint.proxy.type, host, endpoint.port, endpoint.proxy.username,
                                         endpoint.proxySecret.view(), timeout, stopToken);
        if (!tunnel)
        {
            return std::unexpected(connectionError(failureFromProxy(tunnel.error().kind)));
        }
    }
    return transport;
}

template <typename Endpoint>
[[nodiscard]] std::expected<std::unique_ptr<SshByteTransport>, SshBootstrapError>
connectTransportThroughJump(std::unique_ptr<SshByteTransport> upstreamTransport,
                            std::unique_ptr<Libssh2Session> upstreamSession, Endpoint &endpoint,
                            const std::stop_token &stopToken)
{
    std::string host;
    try
    {
        host = endpointHost(endpoint);
    }
    catch (const std::bad_alloc &)
    {
        return std::unexpected(connectionError(SshFailureKind::ProtocolError));
    }
    const std::string &connectHost = endpoint.proxy.type == SshProxyType::None ? host : endpoint.proxy.host;
    const std::uint16_t connectPort = endpoint.proxy.type == SshProxyType::None ? endpoint.port : endpoint.proxy.port;
    const auto timeout = endpointConnectionTimeout(endpoint);
    auto tunneled = SshDirectTcpipTransport::create(std::move(upstreamTransport), std::move(upstreamSession),
                                                    connectHost, connectPort, timeout, stopToken);
    if (!tunneled)
    {
        return std::unexpected(connectionError(sshFailureFromTransport(tunneled.error(), true)));
    }
    std::unique_ptr<SshByteTransport> transport = std::move(*tunneled);
    if (endpoint.proxy.type != SshProxyType::None)
    {
        auto tunnel =
            establishExplicitProxyTunnel(*transport, endpoint.proxy.type, host, endpoint.port, endpoint.proxy.username,
                                         endpoint.proxySecret.view(), timeout, stopToken);
        if (!tunnel)
        {
            return std::unexpected(connectionError(failureFromProxy(tunnel.error().kind)));
        }
    }
    return transport;
}

template <typename Endpoint>
[[nodiscard]] std::expected<std::unique_ptr<Libssh2Session>, SshBootstrapError>
authenticateEndpoint(Endpoint &endpoint, SshByteTransport &transport, const QString &knownHostsPath,
                     const SshConnectionCallbacks &callbacks, const std::stop_token &stopToken)
{
    auto session = Libssh2Session::create();
    if (!session)
    {
        return std::unexpected(connectionError(SshFailureKind::ProtocolError));
    }

    publishPhase(callbacks, SshConnectionPhase::Handshaking);
    auto handshake = (*session)->handshake(transport, endpointConnectionTimeout(endpoint), stopToken);
    if (!handshake)
    {
        return std::unexpected(connectionError(sshFailureFromTransport(handshake.error())));
    }

    publishPhase(callbacks, SshConnectionPhase::VerifyingHostKey);
    auto hostKey = (*session)->hostKey();
    const KnownHostsStore knownHostsStore(knownHostsPath);
    auto knownHosts = knownHostsStore.load();
    if (!hostKey || !knownHosts)
    {
        return std::unexpected(connectionError(SshFailureKind::HostKeyInvalid));
    }

    std::string host;
    try
    {
        host = endpointHost(endpoint);
    }
    catch (const std::bad_alloc &)
    {
        return std::unexpected(connectionError(SshFailureKind::ProtocolError));
    }
    const SshEndpoint observedEndpoint{.host = host, .port = endpoint.port};
    auto trust = (*session)->verifyHostKey(observedEndpoint, *knownHosts);
    if (!trust)
    {
        return std::unexpected(connectionError(SshFailureKind::HostKeyInvalid));
    }

    const QString description = endpointDescription(endpoint);
    const QString algorithm = QString::fromUtf8(hostKeyAlgorithmName(hostKey->algorithm));
    const QString fingerprint = QString::fromStdString(sha256Fingerprint(*hostKey));
    if (*trust == HostKeyTrust::Changed)
    {
        if (callbacks.hostKeyChanged)
        {
            callbacks.hostKeyChanged(description, algorithm, fingerprint);
        }
        return std::unexpected(connectionError(SshFailureKind::HostKeyChanged));
    }
    if (*trust == HostKeyTrust::Unknown)
    {
        publishPhase(callbacks, SshConnectionPhase::AwaitingHostKeyConfirmation);
        const UnknownHostKeyDecision decision =
            callbacks.confirmUnknownHostKey ? callbacks.confirmUnknownHostKey(description, algorithm, fingerprint)
                                            : UnknownHostKeyDecision::Reject;
        if (decision == UnknownHostKeyDecision::Reject)
        {
            return std::unexpected(connectionError(stopToken.stop_requested() ? SshFailureKind::Cancelled
                                                                              : SshFailureKind::HostKeyInvalid));
        }
        knownHosts->push_back(KnownHostEntry{
            .endpoint = observedEndpoint,
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
        trust = (*session)->verifyHostKey(observedEndpoint, *knownHosts);
        if (!trust || *trust != HostKeyTrust::Trusted)
        {
            return std::unexpected(connectionError(SshFailureKind::HostKeyInvalid));
        }
    }

    publishPhase(callbacks, SshConnectionPhase::Authenticating);
    const QByteArray usernameUtf8 = endpoint.username.toUtf8();
    const QByteArray privateKeyPathUtf8 = endpoint.privateKeyPath.toUtf8();
    const std::string username(usernameUtf8.constData(), static_cast<std::size_t>(usernameUtf8.size()));
    const std::string privateKeyPath(privateKeyPathUtf8.constData(),
                                     static_cast<std::size_t>(privateKeyPathUtf8.size()));
    std::expected<void, SshTransportError> authentication;
    switch (endpoint.authentication)
    {
        case SshAuthenticationMethod::PrivateKey:
            authentication =
                (*session)->authenticateWithPrivateKeyFile(transport, username, privateKeyPath, endpoint.secret.view(),
                                                           endpointAuthenticationTimeout(endpoint), stopToken);
            break;
        case SshAuthenticationMethod::Password:
            authentication = (*session)->authenticateWithPassword(transport, username, endpoint.secret.view(),
                                                                  endpointAuthenticationTimeout(endpoint), stopToken);
            break;
        case SshAuthenticationMethod::Agent:
            authentication = (*session)->authenticateWithAgent(transport, username,
                                                               endpointAuthenticationTimeout(endpoint), stopToken);
            break;
    }
    if (!authentication)
    {
        return std::unexpected(connectionError(sshFailureFromTransport(authentication.error())));
    }
    return std::move(*session);
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
    RequestSecretsClearGuard clearSecrets(request);
    if (!validSshConnectionRequest(request))
    {
        return std::unexpected(connectionError(SshFailureKind::ProtocolError));
    }

    publishPhase(callbacks, SshConnectionPhase::Connecting);
    if (request.jumpHosts.empty())
    {
        auto transport = connectInitialTransport(request, stopToken);
        if (!transport)
        {
            return std::unexpected(transport.error());
        }
        auto session = authenticateEndpoint(request, **transport, request.knownHostsPath, callbacks, stopToken);
        if (!session)
        {
            return std::unexpected(session.error());
        }
        return AuthenticatedSshConnection{.transport = std::move(*transport), .session = std::move(*session)};
    }

    auto transport = connectInitialTransport(request.jumpHosts.front(), stopToken);
    if (!transport)
    {
        return std::unexpected(transport.error());
    }
    auto session =
        authenticateEndpoint(request.jumpHosts.front(), **transport, request.knownHostsPath, callbacks, stopToken);
    if (!session)
    {
        return std::unexpected(session.error());
    }

    for (std::size_t index = 1; index < request.jumpHosts.size(); ++index)
    {
        publishPhase(callbacks, SshConnectionPhase::Connecting);
        auto nextTransport = connectTransportThroughJump(std::move(*transport), std::move(*session),
                                                         request.jumpHosts[index], stopToken);
        if (!nextTransport)
        {
            return std::unexpected(nextTransport.error());
        }
        transport = std::move(nextTransport);
        session =
            authenticateEndpoint(request.jumpHosts[index], **transport, request.knownHostsPath, callbacks, stopToken);
        if (!session)
        {
            return std::unexpected(session.error());
        }
    }

    publishPhase(callbacks, SshConnectionPhase::Connecting);
    auto finalTransport = connectTransportThroughJump(std::move(*transport), std::move(*session), request, stopToken);
    if (!finalTransport)
    {
        return std::unexpected(finalTransport.error());
    }
    auto finalSession = authenticateEndpoint(request, **finalTransport, request.knownHostsPath, callbacks, stopToken);
    if (!finalSession)
    {
        return std::unexpected(finalSession.error());
    }
    return AuthenticatedSshConnection{.transport = std::move(*finalTransport), .session = std::move(*finalSession)};
}

} // namespace ztermy::ssh
