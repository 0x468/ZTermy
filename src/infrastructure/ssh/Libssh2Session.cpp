#include "infrastructure/ssh/Libssh2Session.h"

#include <libssh2.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <new>
#include <utility>

namespace
{

[[nodiscard]] ztermy::ssh::SshTransportError mapHandshakeError(const int error) noexcept
{
    using ztermy::ssh::SshTransportError;
    using ztermy::ssh::SshTransportErrorKind;

    switch (error)
    {
        case LIBSSH2_ERROR_SOCKET_DISCONNECT:
        case LIBSSH2_ERROR_SOCKET_RECV:
        case LIBSSH2_ERROR_SOCKET_SEND:
            return SshTransportError{.kind = SshTransportErrorKind::ConnectionLost, .libssh2Code = error};
        default:
            return SshTransportError{.kind = SshTransportErrorKind::ProtocolError, .libssh2Code = error};
    }
}

[[nodiscard]] ztermy::ssh::SocketIoInterest blockedInterest(LIBSSH2_SESSION *session) noexcept
{
    const int directions = libssh2_session_block_directions(session);
    const bool wantsRead = (directions & LIBSSH2_SESSION_BLOCK_INBOUND) != 0;
    const bool wantsWrite = (directions & LIBSSH2_SESSION_BLOCK_OUTBOUND) != 0;

    if (wantsRead && !wantsWrite)
    {
        return ztermy::ssh::SocketIoInterest::Read;
    }
    if (wantsWrite && !wantsRead)
    {
        return ztermy::ssh::SocketIoInterest::Write;
    }
    return ztermy::ssh::SocketIoInterest::ReadWrite;
}

[[nodiscard]] std::expected<void, ztermy::ssh::SshTransportError>
waitForSessionIo(ztermy::ssh::WindowsTcpSocket &socket, LIBSSH2_SESSION *session,
                 const std::chrono::steady_clock::time_point deadline, const std::stop_token &stopToken) noexcept
{
    using ztermy::ssh::SshTransportError;
    using ztermy::ssh::SshTransportErrorKind;
    using ztermy::ssh::TcpConnectErrorKind;

    auto ready = socket.waitUntilReady(blockedInterest(session), deadline, stopToken);
    if (ready)
    {
        return {};
    }

    switch (ready.error().kind)
    {
        case TcpConnectErrorKind::TimedOut:
            return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::TimedOut});
        case TcpConnectErrorKind::Cancelled:
            return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::Cancelled});
        case TcpConnectErrorKind::InvalidEndpoint:
        case TcpConnectErrorKind::NameResolutionFailed:
        case TcpConnectErrorKind::ConnectionRefused:
        case TcpConnectErrorKind::NetworkUnreachable:
        case TcpConnectErrorKind::SystemError:
            return std::unexpected(SshTransportError{
                .kind = SshTransportErrorKind::ConnectionLost,
                .nativeCode = ready.error().nativeCode,
            });
    }
    return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::ConnectionLost});
}

[[nodiscard]] ztermy::ssh::SshTransportError mapAuthenticationError(const int error) noexcept
{
    using ztermy::ssh::SshTransportError;
    using ztermy::ssh::SshTransportErrorKind;

    switch (error)
    {
        case LIBSSH2_ERROR_AUTHENTICATION_FAILED:
            return SshTransportError{.kind = SshTransportErrorKind::AuthenticationRejected, .libssh2Code = error};
        case LIBSSH2_ERROR_METHOD_NOT_SUPPORTED:
        case LIBSSH2_ERROR_PASSWORD_EXPIRED:
            return SshTransportError{.kind = SshTransportErrorKind::AuthenticationUnavailable, .libssh2Code = error};
        case LIBSSH2_ERROR_SOCKET_DISCONNECT:
        case LIBSSH2_ERROR_SOCKET_RECV:
        case LIBSSH2_ERROR_SOCKET_SEND:
            return SshTransportError{.kind = SshTransportErrorKind::ConnectionLost, .libssh2Code = error};
        default:
            return SshTransportError{.kind = SshTransportErrorKind::ProtocolError, .libssh2Code = error};
    }
}

[[nodiscard]] ztermy::ssh::HostKeyAlgorithm mapHostKeyAlgorithm(const int type) noexcept
{
    using ztermy::ssh::HostKeyAlgorithm;

    switch (type)
    {
        case LIBSSH2_HOSTKEY_TYPE_RSA:
            return HostKeyAlgorithm::Rsa;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_256:
            return HostKeyAlgorithm::EcdsaP256;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_384:
            return HostKeyAlgorithm::EcdsaP384;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_521:
            return HostKeyAlgorithm::EcdsaP521;
        case LIBSSH2_HOSTKEY_TYPE_ED25519:
            return HostKeyAlgorithm::Ed25519;
        case LIBSSH2_HOSTKEY_TYPE_DSS:
        case LIBSSH2_HOSTKEY_TYPE_UNKNOWN:
        default:
            return HostKeyAlgorithm::Unknown;
    }
}

} // namespace

namespace ztermy::ssh
{

std::expected<std::unique_ptr<Libssh2Session>, SshTransportError> Libssh2Session::create() noexcept
{
    auto runtime = Libssh2Runtime::create();
    if (!runtime)
    {
        return std::unexpected(SshTransportError{
            .kind = SshTransportErrorKind::InitializationFailed,
            .nativeCode = runtime.error().value(),
        });
    }

    LIBSSH2_SESSION *session = libssh2_session_init();
    if (session == nullptr)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InitializationFailed});
    }
    libssh2_session_set_blocking(session, 0);

    std::unique_ptr<Libssh2Session> result(new (std::nothrow)
                                               Libssh2Session(std::move(*runtime), static_cast<void *>(session)));
    if (!result)
    {
        libssh2_session_free(session);
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InitializationFailed});
    }
    return result;
}

Libssh2Session::Libssh2Session(std::unique_ptr<Libssh2Runtime> runtime, void *session) noexcept
    : m_runtime(std::move(runtime)), m_session(session)
{
}

Libssh2Session::~Libssh2Session()
{
    if (m_session != nullptr)
    {
        libssh2_session_free(static_cast<LIBSSH2_SESSION *>(m_session));
    }
}

std::expected<void, SshTransportError> Libssh2Session::handshake(WindowsTcpSocket &socket,
                                                                 const std::chrono::milliseconds timeout,
                                                                 const std::stop_token &stopToken) noexcept
{
    if (m_session == nullptr || !socket.valid() || m_handshakeComplete)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidState});
    }
    if (timeout <= std::chrono::milliseconds::zero())
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::TimedOut});
    }
    if (stopToken.stop_requested())
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::Cancelled});
    }

    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        const int result = libssh2_session_handshake(session, static_cast<libssh2_socket_t>(socket.nativeHandle()));
        if (result == 0)
        {
            m_handshakeComplete = true;
            return {};
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapHandshakeError(result));
        }

        auto ready = waitForSessionIo(socket, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

bool Libssh2Session::handshakeComplete() const noexcept
{
    return m_handshakeComplete;
}

std::expected<ObservedHostKey, SshTransportError> Libssh2Session::hostKey() const noexcept
{
    if (m_session == nullptr || !m_handshakeComplete)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidState});
    }

    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    std::size_t encodedKeyLength = 0;
    int keyType = LIBSSH2_HOSTKEY_TYPE_UNKNOWN;
    const char *encodedKey = libssh2_session_hostkey(session, &encodedKeyLength, &keyType);
    const char *sha256 = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA256);
    const HostKeyAlgorithm algorithm = mapHostKeyAlgorithm(keyType);
    if (encodedKey == nullptr || encodedKeyLength == 0 || sha256 == nullptr || algorithm == HostKeyAlgorithm::Unknown)
    {
        return std::unexpected(SshTransportError{
            .kind = SshTransportErrorKind::ProtocolError,
            .libssh2Code = libssh2_session_last_errno(session),
        });
    }

    try
    {
        ObservedHostKey result{
            .algorithm = algorithm,
            .encodedKey =
                std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t *>(encodedKey),
                                          reinterpret_cast<const std::uint8_t *>(encodedKey) + encodedKeyLength),
        };
        std::copy_n(reinterpret_cast<const std::uint8_t *>(sha256), result.sha256.size(), result.sha256.begin());
        return result;
    }
    catch (const std::bad_alloc &)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InitializationFailed});
    }
}

std::expected<HostKeyTrust, SshTransportError>
Libssh2Session::verifyHostKey(const SshEndpoint &endpoint, const std::span<const KnownHostEntry> knownHosts) noexcept
{
    auto observed = hostKey();
    if (!observed)
    {
        return std::unexpected(observed.error());
    }

    const HostKeyTrust trust = evaluateHostKeyTrust(endpoint, *observed, knownHosts);
    m_hostKeyVerified = trust == HostKeyTrust::Trusted;
    return trust;
}

std::expected<void, SshTransportError>
Libssh2Session::authenticateWithPassword(WindowsTcpSocket &socket, const std::string_view username,
                                         const std::string_view password, const std::chrono::milliseconds timeout,
                                         const std::stop_token &stopToken) noexcept
{
    if (username.empty() || password.empty()
        || username.size() > static_cast<std::size_t>((std::numeric_limits<unsigned int>::max)())
        || password.size() > static_cast<std::size_t>((std::numeric_limits<unsigned int>::max)()))
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || !socket.valid() || !m_handshakeComplete || !m_hostKeyVerified || m_authenticated)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidState});
    }
    if (timeout <= std::chrono::milliseconds::zero())
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::TimedOut});
    }
    if (stopToken.stop_requested())
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::Cancelled});
    }

    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        const int result =
            libssh2_userauth_password_ex(session, username.data(), static_cast<unsigned int>(username.size()),
                                         password.data(), static_cast<unsigned int>(password.size()), nullptr);
        if (result == 0)
        {
            m_authenticated = true;
            return {};
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapAuthenticationError(result));
        }

        auto ready = waitForSessionIo(socket, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

bool Libssh2Session::authenticated() const noexcept
{
    return m_authenticated;
}

} // namespace ztermy::ssh
