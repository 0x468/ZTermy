#include "infrastructure/ssh/Libssh2Session.h"

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

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
    return wantsRead && wantsWrite ? ztermy::ssh::SocketIoInterest::ReadWrite : ztermy::ssh::SocketIoInterest::Read;
}

[[nodiscard]] ztermy::ssh::SshByteTransport *callbackTransport(void **abstract) noexcept
{
    return abstract == nullptr ? nullptr : static_cast<ztermy::ssh::SshByteTransport *>(*abstract);
}

ssize_t transportSend(libssh2_socket_t, const void *buffer, const size_t length, int, void **abstract) noexcept
{
    auto *transport = callbackTransport(abstract);
    if (transport == nullptr)
    {
        return -1;
    }
    if (length == 0)
    {
        return 0;
    }
    if (buffer == nullptr)
    {
        return -1;
    }

    const auto bytes = std::span(static_cast<const char *>(buffer), length);
    auto written = transport->write(bytes);
    if (written)
    {
        return static_cast<ssize_t>(*written);
    }
    return written.error().kind == ztermy::ssh::SshByteTransportErrorKind::WouldBlock ? -EAGAIN : -1;
}

ssize_t transportReceive(libssh2_socket_t, void *buffer, const size_t length, int, void **abstract) noexcept
{
    auto *transport = callbackTransport(abstract);
    if (transport == nullptr)
    {
        return -1;
    }
    if (length == 0)
    {
        return 0;
    }
    if (buffer == nullptr)
    {
        return -1;
    }

    const auto bytes = std::span(static_cast<char *>(buffer), length);
    auto received = transport->read(bytes);
    if (received)
    {
        return static_cast<ssize_t>(*received);
    }
    if (received.error().kind == ztermy::ssh::SshByteTransportErrorKind::WouldBlock)
    {
        return -EAGAIN;
    }
    return received.error().kind == ztermy::ssh::SshByteTransportErrorKind::Closed ? 0 : -1;
}

[[nodiscard]] std::expected<void, ztermy::ssh::SshTransportError>
waitForSessionIo(ztermy::ssh::SshByteTransport &transport, LIBSSH2_SESSION *session,
                 const std::chrono::steady_clock::time_point deadline, const std::stop_token &stopToken,
                 const std::uintptr_t interruptEvent = 0) noexcept
{
    using ztermy::ssh::SshByteTransportErrorKind;
    using ztermy::ssh::SshTransportError;
    using ztermy::ssh::SshTransportErrorKind;

    auto ready = transport.waitUntilReady(blockedInterest(session), deadline, stopToken, interruptEvent);
    if (ready)
    {
        return {};
    }

    switch (ready.error().kind)
    {
        case SshByteTransportErrorKind::TimedOut:
            return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::TimedOut});
        case SshByteTransportErrorKind::Cancelled:
            return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::Cancelled});
        case SshByteTransportErrorKind::WouldBlock:
        case SshByteTransportErrorKind::Closed:
        case SshByteTransportErrorKind::SystemError:
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

[[nodiscard]] ztermy::ssh::SshTransportError mapPrivateKeyAuthenticationError(const int error) noexcept
{
    using ztermy::ssh::SshTransportError;
    using ztermy::ssh::SshTransportErrorKind;

    switch (error)
    {
        case LIBSSH2_ERROR_AUTHENTICATION_FAILED:
        case LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED:
            return SshTransportError{.kind = SshTransportErrorKind::AuthenticationRejected, .libssh2Code = error};
        case LIBSSH2_ERROR_FILE:
        case LIBSSH2_ERROR_KEYFILE_AUTH_FAILED:
        case LIBSSH2_ERROR_METHOD_NOT_SUPPORTED:
            return SshTransportError{.kind = SshTransportErrorKind::AuthenticationUnavailable, .libssh2Code = error};
        case LIBSSH2_ERROR_ALLOC:
            return SshTransportError{.kind = SshTransportErrorKind::InitializationFailed, .libssh2Code = error};
        case LIBSSH2_ERROR_SOCKET_DISCONNECT:
        case LIBSSH2_ERROR_SOCKET_RECV:
        case LIBSSH2_ERROR_SOCKET_SEND:
            return SshTransportError{.kind = SshTransportErrorKind::ConnectionLost, .libssh2Code = error};
        default:
            return SshTransportError{.kind = SshTransportErrorKind::ProtocolError, .libssh2Code = error};
    }
}

[[nodiscard]] ztermy::ssh::SshTransportError mapChannelError(const int error) noexcept
{
    using ztermy::ssh::SshTransportError;
    using ztermy::ssh::SshTransportErrorKind;

    switch (error)
    {
        case LIBSSH2_ERROR_ALLOC:
            return SshTransportError{.kind = SshTransportErrorKind::InitializationFailed, .libssh2Code = error};
        case LIBSSH2_ERROR_CHANNEL_CLOSED:
        case LIBSSH2_ERROR_CHANNEL_EOF_SENT:
        case LIBSSH2_ERROR_SOCKET_DISCONNECT:
        case LIBSSH2_ERROR_SOCKET_RECV:
        case LIBSSH2_ERROR_SOCKET_SEND:
            return SshTransportError{.kind = SshTransportErrorKind::ConnectionLost, .libssh2Code = error};
        default:
            return SshTransportError{.kind = SshTransportErrorKind::ProtocolError, .libssh2Code = error};
    }
}

[[nodiscard]] ztermy::ssh::SshTransportError mapSftpError(LIBSSH2_SESSION *session, LIBSSH2_SFTP *sftp,
                                                          const int error) noexcept
{
    ztermy::ssh::SshTransportError mapped = mapChannelError(error);
    if (error == LIBSSH2_ERROR_SFTP_PROTOCOL && sftp != nullptr)
    {
        mapped.nativeCode = static_cast<int>(libssh2_sftp_last_error(sftp));
    }
    else if (error == 0 && session != nullptr)
    {
        mapped = mapChannelError(libssh2_session_last_errno(session));
    }
    return mapped;
}

[[nodiscard]] bool validSftpPath(const std::string_view path) noexcept
{
    return !path.empty() && path.front() == '/' && path.find('\0') == std::string_view::npos
           && path.size() <= static_cast<std::size_t>((std::numeric_limits<unsigned int>::max)());
}

[[nodiscard]] ztermy::sftp::EntryType entryType(const LIBSSH2_SFTP_ATTRIBUTES &attributes) noexcept
{
    if ((attributes.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) == 0)
    {
        return ztermy::sftp::EntryType::Other;
    }
    if (LIBSSH2_SFTP_S_ISREG(attributes.permissions))
    {
        return ztermy::sftp::EntryType::RegularFile;
    }
    if (LIBSSH2_SFTP_S_ISDIR(attributes.permissions))
    {
        return ztermy::sftp::EntryType::Directory;
    }
    if (LIBSSH2_SFTP_S_ISLNK(attributes.permissions))
    {
        return ztermy::sftp::EntryType::SymbolicLink;
    }
    return ztermy::sftp::EntryType::Other;
}

[[nodiscard]] bool validTerminalDimensions(const std::uint32_t columns, const std::uint32_t rows) noexcept
{
    constexpr auto maximum = static_cast<std::uint32_t>((std::numeric_limits<int>::max)());
    return columns > 0 && rows > 0 && columns <= maximum && rows <= maximum;
}

class SensitiveString final
{
public:
    explicit SensitiveString(const std::string_view value) : m_value(value) {}

    ~SensitiveString()
    {
        if (!m_value.empty())
        {
            SecureZeroMemory(m_value.data(), m_value.size());
        }
    }

    SensitiveString(const SensitiveString &) = delete;
    SensitiveString &operator=(const SensitiveString &) = delete;
    SensitiveString(SensitiveString &&) = delete;
    SensitiveString &operator=(SensitiveString &&) = delete;

    [[nodiscard]] const char *c_str() const noexcept { return m_value.c_str(); }

private:
    std::string m_value;
};

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
    if (m_sftpFile != nullptr)
    {
        libssh2_sftp_close_handle(static_cast<LIBSSH2_SFTP_HANDLE *>(m_sftpFile));
    }
    if (m_sftp != nullptr)
    {
        libssh2_sftp_shutdown(static_cast<LIBSSH2_SFTP *>(m_sftp));
    }
    if (m_auxiliaryChannel != nullptr)
    {
        libssh2_channel_free(static_cast<LIBSSH2_CHANNEL *>(m_auxiliaryChannel));
    }
    if (m_terminalChannel != nullptr)
    {
        libssh2_channel_free(static_cast<LIBSSH2_CHANNEL *>(m_terminalChannel));
    }
    if (m_directTcpipChannel != nullptr)
    {
        libssh2_channel_free(static_cast<LIBSSH2_CHANNEL *>(m_directTcpipChannel));
    }
    for (const auto &entry : m_forwardingChannels)
    {
        libssh2_channel_free(static_cast<LIBSSH2_CHANNEL *>(entry.channel));
    }
    for (const auto &entry : m_remoteForwardListeners)
    {
        libssh2_channel_forward_cancel(static_cast<LIBSSH2_LISTENER *>(entry.listener));
    }
    if (m_session != nullptr)
    {
        libssh2_session_free(static_cast<LIBSSH2_SESSION *>(m_session));
    }
}

bool Libssh2Session::usesTransport(const SshByteTransport &transport) const noexcept
{
    return m_transport == &transport && transport.valid();
}

void *Libssh2Session::forwardingChannel(const std::uint64_t id) const noexcept
{
    const auto entry = std::ranges::find(m_forwardingChannels, id, &ForwardingChannelEntry::id);
    return entry == m_forwardingChannels.end() ? nullptr : entry->channel;
}

void *Libssh2Session::remoteForwardListener(const std::uint64_t id) const noexcept
{
    const auto entry = std::ranges::find(m_remoteForwardListeners, id, &RemoteForwardListenerEntry::id);
    return entry == m_remoteForwardListeners.end() ? nullptr : entry->listener;
}

std::expected<void, SshTransportError> Libssh2Session::handshake(SshByteTransport &transport,
                                                                 const std::chrono::milliseconds timeout,
                                                                 const std::stop_token &stopToken) noexcept
{
    if (m_session == nullptr || !transport.valid() || m_transport != nullptr || m_handshakeComplete)
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
    *libssh2_session_abstract(session) = &transport;
    (void)libssh2_session_callback_set2(session, LIBSSH2_CALLBACK_SEND,
                                        reinterpret_cast<libssh2_cb_generic *>(transportSend));
    (void)libssh2_session_callback_set2(session, LIBSSH2_CALLBACK_RECV,
                                        reinterpret_cast<libssh2_cb_generic *>(transportReceive));
    m_transport = &transport;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        const int result = libssh2_session_handshake(session, libssh2_socket_t{0});
        if (result == 0)
        {
            m_handshakeComplete = true;
            return {};
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapHandshakeError(result));
        }

        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
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
Libssh2Session::authenticateWithPassword(SshByteTransport &transport, const std::string_view username,
                                         const std::string_view password, const std::chrono::milliseconds timeout,
                                         const std::stop_token &stopToken) noexcept
{
    if (username.empty() || password.empty()
        || username.size() > static_cast<std::size_t>((std::numeric_limits<unsigned int>::max)())
        || password.size() > static_cast<std::size_t>((std::numeric_limits<unsigned int>::max)()))
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || !usesTransport(transport) || !m_handshakeComplete || !m_hostKeyVerified
        || m_authenticated)
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

        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

std::expected<void, SshTransportError>
Libssh2Session::authenticateWithPrivateKeyFile(SshByteTransport &transport, const std::string_view username,
                                               const std::string_view privateKeyPath, const std::string_view passphrase,
                                               const std::chrono::milliseconds timeout,
                                               const std::stop_token &stopToken) noexcept
{
    if (username.empty() || privateKeyPath.empty() || privateKeyPath.find('\0') != std::string_view::npos
        || passphrase.find('\0') != std::string_view::npos
        || username.size() > static_cast<std::size_t>((std::numeric_limits<unsigned int>::max)()))
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || !usesTransport(transport) || !m_handshakeComplete || !m_hostKeyVerified
        || m_authenticated)
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

    try
    {
        const std::string privateKeyPathCopy(privateKeyPath);
        const SensitiveString passphraseCopy(passphrase);
        auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true)
        {
            const int result = libssh2_userauth_publickey_fromfile_ex(
                session, username.data(), static_cast<unsigned int>(username.size()), nullptr,
                privateKeyPathCopy.c_str(), passphraseCopy.c_str());
            if (result == 0)
            {
                m_authenticated = true;
                return {};
            }
            if (result != LIBSSH2_ERROR_EAGAIN)
            {
                return std::unexpected(mapPrivateKeyAuthenticationError(result));
            }

            auto ready = waitForSessionIo(transport, session, deadline, stopToken);
            if (!ready)
            {
                return std::unexpected(ready.error());
            }
        }
    }
    catch (const std::bad_alloc &)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InitializationFailed});
    }
}

std::expected<void, SshTransportError> Libssh2Session::authenticateWithAgent(SshByteTransport &transport,
                                                                             const std::string_view username,
                                                                             const std::chrono::milliseconds timeout,
                                                                             const std::stop_token &stopToken) noexcept
{
    if (username.empty() || username.find('\0') != std::string_view::npos
        || username.size() > static_cast<std::size_t>((std::numeric_limits<unsigned int>::max)()))
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || !usesTransport(transport) || !m_handshakeComplete || !m_hostKeyVerified
        || m_authenticated)
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

    std::string usernameCopy;
    try
    {
        usernameCopy.assign(username);
    }
    catch (const std::bad_alloc &)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InitializationFailed});
    }

    struct AgentGuard final
    {
        LIBSSH2_AGENT *agent = nullptr;
        bool connected = false;

        ~AgentGuard()
        {
            if (connected)
            {
                (void)libssh2_agent_disconnect(agent);
            }
            if (agent != nullptr)
            {
                libssh2_agent_free(agent);
            }
        }
    };

    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    AgentGuard guard{.agent = libssh2_agent_init(session)};
    if (guard.agent == nullptr)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InitializationFailed});
    }
    if (const int result = libssh2_agent_connect(guard.agent); result != 0)
    {
        return std::unexpected(
            SshTransportError{.kind = SshTransportErrorKind::AuthenticationUnavailable, .libssh2Code = result});
    }
    guard.connected = true;
    if (const int result = libssh2_agent_list_identities(guard.agent); result != 0)
    {
        return std::unexpected(
            SshTransportError{.kind = SshTransportErrorKind::AuthenticationUnavailable, .libssh2Code = result});
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    libssh2_agent_publickey *previous = nullptr;
    bool offeredIdentity = false;
    while (true)
    {
        libssh2_agent_publickey *identity = nullptr;
        const int identityResult = libssh2_agent_get_identity(guard.agent, &identity, previous);
        if (identityResult == 1)
        {
            return std::unexpected(SshTransportError{.kind = offeredIdentity
                                                                 ? SshTransportErrorKind::AuthenticationRejected
                                                                 : SshTransportErrorKind::AuthenticationUnavailable});
        }
        if (identityResult != 0 || identity == nullptr)
        {
            return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::AuthenticationUnavailable,
                                                     .libssh2Code = identityResult});
        }
        offeredIdentity = true;
        previous = identity;

        while (true)
        {
            if (stopToken.stop_requested())
            {
                return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::Cancelled});
            }
            const int result = libssh2_agent_userauth(guard.agent, usernameCopy.c_str(), identity);
            if (result == 0)
            {
                m_authenticated = true;
                return {};
            }
            if (result == LIBSSH2_ERROR_AUTHENTICATION_FAILED || result == LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED)
            {
                break;
            }
            if (result != LIBSSH2_ERROR_EAGAIN)
            {
                return std::unexpected(mapPrivateKeyAuthenticationError(result));
            }
            auto ready = waitForSessionIo(transport, session, deadline, stopToken);
            if (!ready)
            {
                return std::unexpected(ready.error());
            }
        }
    }
}

bool Libssh2Session::authenticated() const noexcept
{
    return m_authenticated;
}

std::expected<void, SshTransportError>
Libssh2Session::openDirectTcpip(SshByteTransport &transport, const std::string_view host, const std::uint16_t port,
                                const std::chrono::milliseconds timeout, const std::stop_token &stopToken) noexcept
{
    if (m_session == nullptr || !m_authenticated || m_directTcpipChannel != nullptr || !usesTransport(transport)
        || host.empty() || port == 0)
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

    std::string targetHost;
    try
    {
        targetHost = host;
    }
    catch (const std::bad_alloc &)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InitializationFailed});
    }

    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        auto *channel = libssh2_channel_direct_tcpip_ex(session, targetHost.c_str(), port, "127.0.0.1", 0);
        if (channel != nullptr)
        {
            m_directTcpipChannel = channel;
            return {};
        }
        const int error = libssh2_session_last_errno(session);
        if (error != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapChannelError(error));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

std::expected<std::size_t, SshByteTransportError> Libssh2Session::readDirectTcpip(const std::span<char> output) noexcept
{
    if (m_directTcpipChannel == nullptr)
    {
        return std::unexpected(
            SshByteTransportError{.kind = SshByteTransportErrorKind::SystemError, .nativeCode = LIBSSH2_ERROR_BAD_USE});
    }
    if (output.empty())
    {
        return std::size_t{0};
    }
    auto *channel = static_cast<LIBSSH2_CHANNEL *>(m_directTcpipChannel);
    const auto length = (std::min)(output.size(), static_cast<std::size_t>((std::numeric_limits<int>::max)()));
    const ssize_t result = libssh2_channel_read_ex(channel, 0, output.data(), length);
    if (result >= 0)
    {
        return static_cast<std::size_t>(result);
    }
    if (result == LIBSSH2_ERROR_EAGAIN)
    {
        return std::unexpected(SshByteTransportError{.kind = SshByteTransportErrorKind::WouldBlock});
    }
    return std::unexpected(SshByteTransportError{.kind = libssh2_channel_eof(channel) != 0
                                                             ? SshByteTransportErrorKind::Closed
                                                             : SshByteTransportErrorKind::SystemError,
                                                 .nativeCode = static_cast<int>(result)});
}

std::expected<std::size_t, SshByteTransportError>
Libssh2Session::writeDirectTcpip(const std::span<const char> input) noexcept
{
    if (m_directTcpipChannel == nullptr)
    {
        return std::unexpected(
            SshByteTransportError{.kind = SshByteTransportErrorKind::SystemError, .nativeCode = LIBSSH2_ERROR_BAD_USE});
    }
    if (input.empty())
    {
        return std::size_t{0};
    }
    auto *channel = static_cast<LIBSSH2_CHANNEL *>(m_directTcpipChannel);
    const auto length = (std::min)(input.size(), static_cast<std::size_t>((std::numeric_limits<int>::max)()));
    const ssize_t result = libssh2_channel_write_ex(channel, 0, input.data(), length);
    if (result >= 0)
    {
        return static_cast<std::size_t>(result);
    }
    if (result == LIBSSH2_ERROR_EAGAIN)
    {
        return std::unexpected(SshByteTransportError{.kind = SshByteTransportErrorKind::WouldBlock});
    }
    return std::unexpected(SshByteTransportError{.kind = libssh2_channel_eof(channel) != 0
                                                             ? SshByteTransportErrorKind::Closed
                                                             : SshByteTransportErrorKind::SystemError,
                                                 .nativeCode = static_cast<int>(result)});
}

std::expected<void, SshByteTransportError>
Libssh2Session::waitDirectTcpip(SshByteTransport &transport, const std::chrono::steady_clock::time_point deadline,
                                const std::stop_token &stopToken, const std::uintptr_t interruptHandle) noexcept
{
    if (m_session == nullptr || m_directTcpipChannel == nullptr || !usesTransport(transport))
    {
        return std::unexpected(
            SshByteTransportError{.kind = SshByteTransportErrorKind::SystemError, .nativeCode = LIBSSH2_ERROR_BAD_USE});
    }
    return transport.waitUntilReady(blockedInterest(static_cast<LIBSSH2_SESSION *>(m_session)), deadline, stopToken,
                                    interruptHandle);
}

std::expected<void, SshTransportError> Libssh2Session::closeDirectTcpip(SshByteTransport &transport,
                                                                        const std::chrono::milliseconds timeout,
                                                                        const std::stop_token &stopToken) noexcept
{
    if (m_session == nullptr || m_directTcpipChannel == nullptr || !usesTransport(transport))
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
    auto *channel = static_cast<LIBSSH2_CHANNEL *>(m_directTcpipChannel);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        const int result = libssh2_channel_close(channel);
        if (result == 0)
        {
            break;
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapChannelError(result));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }

    const int freeResult = libssh2_channel_free(channel);
    if (freeResult != 0)
    {
        return std::unexpected(mapChannelError(freeResult));
    }
    m_directTcpipChannel = nullptr;
    return {};
}

bool Libssh2Session::directTcpipOpen() const noexcept
{
    return m_directTcpipChannel != nullptr;
}

std::expected<SshForwardingChannel, SshTransportError>
Libssh2Session::openForwardingChannel(SshByteTransport &transport, const std::string_view host,
                                      const std::uint16_t port, const std::string_view originatorHost,
                                      const std::uint16_t originatorPort, const std::chrono::milliseconds timeout,
                                      const std::stop_token &stopToken) noexcept
{
    constexpr std::size_t maximumHostBytes = 1'024;
    if (host.empty() || port == 0 || host.size() > maximumHostBytes || host.find('\0') != std::string_view::npos
        || originatorHost.empty() || originatorHost.size() > maximumHostBytes
        || originatorHost.find('\0') != std::string_view::npos)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || !m_authenticated || !usesTransport(transport))
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidState});
    }
    if (m_nextForwardingResourceId == 0)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InitializationFailed});
    }
    if (timeout <= std::chrono::milliseconds::zero())
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::TimedOut});
    }
    if (stopToken.stop_requested())
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::Cancelled});
    }
    std::string targetHost;
    std::string sourceHost;
    try
    {
        targetHost = host;
        sourceHost = originatorHost;
    }
    catch (const std::bad_alloc &)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InitializationFailed});
    }

    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        auto *channel =
            libssh2_channel_direct_tcpip_ex(session, targetHost.c_str(), port, sourceHost.c_str(), originatorPort);
        if (channel != nullptr)
        {
            const SshForwardingChannel result{.id = m_nextForwardingResourceId++};
            try
            {
                m_forwardingChannels.push_back(ForwardingChannelEntry{.id = result.id, .channel = channel});
            }
            catch (const std::bad_alloc &)
            {
                libssh2_channel_free(channel);
                return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InitializationFailed});
            }
            return result;
        }
        const int error = libssh2_session_last_errno(session);
        if (error != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapChannelError(error));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

std::expected<std::size_t, SshByteTransportError>
Libssh2Session::readForwardingChannel(const SshForwardingChannel forwarding, const std::span<char> output) noexcept
{
    auto *channel = static_cast<LIBSSH2_CHANNEL *>(forwardingChannel(forwarding.id));
    if (channel == nullptr)
    {
        return std::unexpected(
            SshByteTransportError{.kind = SshByteTransportErrorKind::SystemError, .nativeCode = LIBSSH2_ERROR_BAD_USE});
    }
    if (output.empty())
    {
        return std::size_t{0};
    }
    const auto length = (std::min)(output.size(), static_cast<std::size_t>((std::numeric_limits<int>::max)()));
    const ssize_t result = libssh2_channel_read_ex(channel, 0, output.data(), length);
    if (result >= 0)
    {
        return static_cast<std::size_t>(result);
    }
    if (result == LIBSSH2_ERROR_EAGAIN)
    {
        return std::unexpected(SshByteTransportError{.kind = SshByteTransportErrorKind::WouldBlock});
    }
    return std::unexpected(SshByteTransportError{.kind = libssh2_channel_eof(channel) != 0
                                                             ? SshByteTransportErrorKind::Closed
                                                             : SshByteTransportErrorKind::SystemError,
                                                 .nativeCode = static_cast<int>(result)});
}

std::expected<std::size_t, SshByteTransportError>
Libssh2Session::writeForwardingChannel(const SshForwardingChannel forwarding,
                                       const std::span<const char> input) noexcept
{
    auto *channel = static_cast<LIBSSH2_CHANNEL *>(forwardingChannel(forwarding.id));
    if (channel == nullptr)
    {
        return std::unexpected(
            SshByteTransportError{.kind = SshByteTransportErrorKind::SystemError, .nativeCode = LIBSSH2_ERROR_BAD_USE});
    }
    if (input.empty())
    {
        return std::size_t{0};
    }
    const auto length = (std::min)(input.size(), static_cast<std::size_t>((std::numeric_limits<int>::max)()));
    const ssize_t result = libssh2_channel_write_ex(channel, 0, input.data(), length);
    if (result >= 0)
    {
        return static_cast<std::size_t>(result);
    }
    if (result == LIBSSH2_ERROR_EAGAIN)
    {
        return std::unexpected(SshByteTransportError{.kind = SshByteTransportErrorKind::WouldBlock});
    }
    return std::unexpected(SshByteTransportError{.kind = libssh2_channel_eof(channel) != 0
                                                             ? SshByteTransportErrorKind::Closed
                                                             : SshByteTransportErrorKind::SystemError,
                                                 .nativeCode = static_cast<int>(result)});
}

bool Libssh2Session::forwardingChannelEof(const SshForwardingChannel forwarding) const noexcept
{
    auto *channel = static_cast<LIBSSH2_CHANNEL *>(forwardingChannel(forwarding.id));
    return channel == nullptr || libssh2_channel_eof(channel) != 0;
}

std::expected<void, SshTransportError>
Libssh2Session::sendForwardingChannelEof(SshByteTransport &transport, const SshForwardingChannel forwarding,
                                         const std::chrono::milliseconds timeout,
                                         const std::stop_token &stopToken) noexcept
{
    auto *channel = static_cast<LIBSSH2_CHANNEL *>(forwardingChannel(forwarding.id));
    if (m_session == nullptr || channel == nullptr || !usesTransport(transport))
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
        const int result = libssh2_channel_send_eof(channel);
        if (result == 0)
        {
            return {};
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapChannelError(result));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

std::expected<void, SshTransportError> Libssh2Session::closeForwardingChannel(SshByteTransport &transport,
                                                                              const SshForwardingChannel forwarding,
                                                                              const std::chrono::milliseconds timeout,
                                                                              const std::stop_token &stopToken) noexcept
{
    const auto entry = std::ranges::find(m_forwardingChannels, forwarding.id, &ForwardingChannelEntry::id);
    if (m_session == nullptr || entry == m_forwardingChannels.end() || !usesTransport(transport))
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
    auto *channel = static_cast<LIBSSH2_CHANNEL *>(entry->channel);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        const int result = libssh2_channel_close(channel);
        if (result == 0)
        {
            break;
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            libssh2_channel_free(channel);
            m_forwardingChannels.erase(entry);
            return std::unexpected(mapChannelError(result));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
    const int freeResult = libssh2_channel_free(channel);
    m_forwardingChannels.erase(entry);
    if (freeResult != 0)
    {
        return std::unexpected(mapChannelError(freeResult));
    }
    return {};
}

std::expected<SshRemoteForwardListener, SshTransportError> Libssh2Session::openRemoteForwardListener(
    SshByteTransport &transport, const std::string_view bindHost, const std::uint16_t port,
    const std::uint32_t queueSize, const std::chrono::milliseconds timeout, const std::stop_token &stopToken) noexcept
{
    constexpr std::size_t maximumHostBytes = 1'024;
    constexpr std::uint32_t maximumQueueSize = 128;
    if (bindHost.empty() || bindHost.size() > maximumHostBytes || bindHost.find('\0') != std::string_view::npos
        || queueSize == 0 || queueSize > maximumQueueSize)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || !m_authenticated || !usesTransport(transport))
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidState});
    }
    if (m_nextForwardingResourceId == 0)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InitializationFailed});
    }
    if (timeout <= std::chrono::milliseconds::zero())
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::TimedOut});
    }
    if (stopToken.stop_requested())
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::Cancelled});
    }
    std::string address;
    try
    {
        address = bindHost;
    }
    catch (const std::bad_alloc &)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InitializationFailed});
    }
    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        int boundPort = 0;
        auto *listener =
            libssh2_channel_forward_listen_ex(session, address.c_str(), port, &boundPort, static_cast<int>(queueSize));
        if (listener != nullptr)
        {
            if (boundPort <= 0 || boundPort > (std::numeric_limits<std::uint16_t>::max)())
            {
                libssh2_channel_forward_cancel(listener);
                return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::ProtocolError});
            }
            const SshRemoteForwardListener result{.id = m_nextForwardingResourceId++,
                                                  .boundPort = static_cast<std::uint16_t>(boundPort)};
            try
            {
                m_remoteForwardListeners.push_back(RemoteForwardListenerEntry{.id = result.id, .listener = listener});
            }
            catch (const std::bad_alloc &)
            {
                libssh2_channel_forward_cancel(listener);
                return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InitializationFailed});
            }
            return result;
        }
        const int error = libssh2_session_last_errno(session);
        if (error != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapChannelError(error));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

std::expected<std::optional<SshForwardingChannel>, SshTransportError>
Libssh2Session::acceptRemoteForwardingChannel(const SshRemoteForwardListener remote) noexcept
{
    auto *listener = static_cast<LIBSSH2_LISTENER *>(remoteForwardListener(remote.id));
    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    if (session == nullptr || listener == nullptr || remote.boundPort == 0 || m_nextForwardingResourceId == 0)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidState});
    }
    auto *channel = libssh2_channel_forward_accept(listener);
    if (channel == nullptr)
    {
        const int error = libssh2_session_last_errno(session);
        if (error == LIBSSH2_ERROR_EAGAIN)
        {
            return std::optional<SshForwardingChannel>{};
        }
        return std::unexpected(mapChannelError(error));
    }
    const SshForwardingChannel result{.id = m_nextForwardingResourceId++};
    try
    {
        m_forwardingChannels.push_back(ForwardingChannelEntry{.id = result.id, .channel = channel});
    }
    catch (const std::bad_alloc &)
    {
        libssh2_channel_free(channel);
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InitializationFailed});
    }
    return std::optional{result};
}

std::expected<void, SshTransportError>
Libssh2Session::closeRemoteForwardListener(SshByteTransport &transport, const SshRemoteForwardListener remote,
                                           const std::chrono::milliseconds timeout,
                                           const std::stop_token &stopToken) noexcept
{
    const auto entry = std::ranges::find(m_remoteForwardListeners, remote.id, &RemoteForwardListenerEntry::id);
    if (m_session == nullptr || entry == m_remoteForwardListeners.end() || !usesTransport(transport))
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
    auto *listener = static_cast<LIBSSH2_LISTENER *>(entry->listener);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        const int result = libssh2_channel_forward_cancel(listener);
        if (result == 0)
        {
            m_remoteForwardListeners.erase(entry);
            return {};
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapChannelError(result));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

std::expected<void, SshByteTransportError>
Libssh2Session::waitForwardingIo(SshByteTransport &transport, const std::chrono::steady_clock::time_point deadline,
                                 const std::stop_token &stopToken, const std::uintptr_t interruptHandle) noexcept
{
    if (m_session == nullptr || !usesTransport(transport))
    {
        return std::unexpected(
            SshByteTransportError{.kind = SshByteTransportErrorKind::SystemError, .nativeCode = LIBSSH2_ERROR_BAD_USE});
    }
    return transport.waitUntilReady(blockedInterest(static_cast<LIBSSH2_SESSION *>(m_session)), deadline, stopToken,
                                    interruptHandle);
}

std::expected<void, SshTransportError>
Libssh2Session::openTerminal(SshByteTransport &transport, const std::uint32_t columns, const std::uint32_t rows,
                             const std::string_view terminalType, const std::chrono::milliseconds timeout,
                             const std::stop_token &stopToken) noexcept
{
    return openTerminal(transport, columns, rows, terminalType, std::span<const SshTerminalEnvironment>{}, timeout,
                        stopToken);
}

std::expected<void, SshTransportError>
Libssh2Session::openTerminal(SshByteTransport &transport, const std::uint32_t columns, const std::uint32_t rows,
                             const std::string_view terminalType,
                             const std::span<const SshTerminalEnvironment> environment,
                             const std::chrono::milliseconds timeout, const std::stop_token &stopToken) noexcept
{
    if (!validTerminalDimensions(columns, rows) || terminalType.empty()
        || terminalType.find('\0') != std::string_view::npos
        || terminalType.size() > static_cast<std::size_t>((std::numeric_limits<unsigned int>::max)()))
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || !usesTransport(transport) || !m_authenticated || m_terminalChannel != nullptr
        || m_sftp != nullptr)
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
    LIBSSH2_CHANNEL *channel = nullptr;
    while (channel == nullptr)
    {
        channel = libssh2_channel_open_session(session);
        if (channel != nullptr)
        {
            break;
        }
        const int error = libssh2_session_last_errno(session);
        if (error != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapChannelError(error));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }

    const auto releaseChannel = [&channel]() noexcept {
        libssh2_channel_free(channel);
        channel = nullptr;
    };

    while (true)
    {
        const int result =
            libssh2_channel_request_pty_ex(channel, terminalType.data(), static_cast<unsigned int>(terminalType.size()),
                                           nullptr, 0, static_cast<int>(columns), static_cast<int>(rows), 0, 0);
        if (result == 0)
        {
            break;
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            const SshTransportError error = mapChannelError(result);
            releaseChannel();
            return std::unexpected(error);
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            const SshTransportError error = ready.error();
            releaseChannel();
            return std::unexpected(error);
        }
    }

    for (const SshTerminalEnvironment &variable : environment)
    {
        if (variable.name.empty() || variable.name.find('\0') != std::string_view::npos
            || variable.value.find('\0') != std::string_view::npos
            || variable.name.size() > static_cast<std::size_t>((std::numeric_limits<unsigned int>::max)())
            || variable.value.size() > static_cast<std::size_t>((std::numeric_limits<unsigned int>::max)()))
        {
            releaseChannel();
            return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
        }
        while (true)
        {
            const int result = libssh2_channel_setenv_ex(
                channel, variable.name.data(), static_cast<unsigned int>(variable.name.size()), variable.value.data(),
                static_cast<unsigned int>(variable.value.size()));
            if (result == 0)
            {
                break;
            }
            if (result != LIBSSH2_ERROR_EAGAIN)
            {
                const SshTransportError error = mapChannelError(result);
                releaseChannel();
                return std::unexpected(error);
            }
            auto ready = waitForSessionIo(transport, session, deadline, stopToken);
            if (!ready)
            {
                const SshTransportError error = ready.error();
                releaseChannel();
                return std::unexpected(error);
            }
        }
    }

    while (true)
    {
        const int result = libssh2_channel_shell(channel);
        if (result == 0)
        {
            m_terminalChannel = channel;
            return {};
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            const SshTransportError error = mapChannelError(result);
            releaseChannel();
            return std::unexpected(error);
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            const SshTransportError error = ready.error();
            releaseChannel();
            return std::unexpected(error);
        }
    }
}

std::expected<void, SshTransportError> Libssh2Session::configureKeepalive(const std::uint32_t intervalSeconds) noexcept
{
    if (m_session == nullptr || !m_authenticated)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidState});
    }
    libssh2_keepalive_config(static_cast<LIBSSH2_SESSION *>(m_session), 1, static_cast<unsigned int>(intervalSeconds));
    return {};
}

std::expected<int, SshTransportError> Libssh2Session::sendKeepalive() noexcept
{
    if (m_session == nullptr || !m_authenticated)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidState});
    }
    int secondsToNext = 0;
    const int result = libssh2_keepalive_send(static_cast<LIBSSH2_SESSION *>(m_session), &secondsToNext);
    if (result != 0)
    {
        return std::unexpected(mapChannelError(result));
    }
    return secondsToNext;
}

std::expected<std::size_t, SshTransportError> Libssh2Session::readTerminal(SshByteTransport &transport,
                                                                           const std::span<char> output,
                                                                           const std::chrono::milliseconds timeout,
                                                                           const std::stop_token &stopToken,
                                                                           const std::uintptr_t interruptEvent) noexcept
{
    if (output.empty())
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || m_terminalChannel == nullptr || !usesTransport(transport))
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
    auto *channel = static_cast<LIBSSH2_CHANNEL *>(m_terminalChannel);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        const auto result = libssh2_channel_read(channel, output.data(), output.size());
        if (result >= 0)
        {
            return static_cast<std::size_t>(result);
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapChannelError(static_cast<int>(result)));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken, interruptEvent);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

std::expected<void, SshTransportError> Libssh2Session::writeTerminal(SshByteTransport &transport,
                                                                     const std::span<const char> input,
                                                                     const std::chrono::milliseconds timeout,
                                                                     const std::stop_token &stopToken) noexcept
{
    if (input.empty())
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || m_terminalChannel == nullptr || !usesTransport(transport))
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
    auto *channel = static_cast<LIBSSH2_CHANNEL *>(m_terminalChannel);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::size_t written = 0;
    while (written < input.size())
    {
        const auto result = libssh2_channel_write(channel, input.data() + written, input.size() - written);
        if (result > 0)
        {
            written += static_cast<std::size_t>(result);
            continue;
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapChannelError(static_cast<int>(result)));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
    return {};
}

std::expected<void, SshTransportError>
Libssh2Session::resizeTerminal(SshByteTransport &transport, const std::uint32_t columns, const std::uint32_t rows,
                               const std::chrono::milliseconds timeout, const std::stop_token &stopToken) noexcept
{
    if (!validTerminalDimensions(columns, rows))
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || m_terminalChannel == nullptr || !usesTransport(transport))
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
    auto *channel = static_cast<LIBSSH2_CHANNEL *>(m_terminalChannel);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        const int result = libssh2_channel_request_pty_size(channel, static_cast<int>(columns), static_cast<int>(rows));
        if (result == 0)
        {
            return {};
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapChannelError(result));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

std::expected<void, SshTransportError> Libssh2Session::closeTerminal(SshByteTransport &transport,
                                                                     const std::chrono::milliseconds timeout,
                                                                     const std::stop_token &stopToken) noexcept
{
    if (m_session == nullptr || m_terminalChannel == nullptr || !usesTransport(transport))
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
    auto *channel = static_cast<LIBSSH2_CHANNEL *>(m_terminalChannel);
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true)
    {
        const int result = libssh2_channel_close(channel);
        if (result == 0)
        {
            break;
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapChannelError(result));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }

    const int freeResult = libssh2_channel_free(channel);
    if (freeResult != 0)
    {
        return std::unexpected(mapChannelError(freeResult));
    }
    m_terminalChannel = nullptr;
    return {};
}

bool Libssh2Session::terminalOpen() const noexcept
{
    return m_terminalChannel != nullptr;
}

std::expected<void, SshTransportError> Libssh2Session::openSftp(SshByteTransport &transport,
                                                                const std::chrono::milliseconds timeout,
                                                                const std::stop_token &stopToken) noexcept
{
    if (m_session == nullptr || !usesTransport(transport) || !m_authenticated || m_sftp != nullptr
        || m_terminalChannel != nullptr || m_auxiliaryChannel != nullptr)
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
        LIBSSH2_SFTP *sftp = libssh2_sftp_init(session);
        if (sftp != nullptr)
        {
            m_sftp = sftp;
            return {};
        }
        const int error = libssh2_session_last_errno(session);
        if (error != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapSftpError(session, nullptr, error));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

std::expected<std::vector<sftp::DirectoryEntry>, SshTransportError>
Libssh2Session::listSftpDirectory(SshByteTransport &transport, const std::string_view remotePath,
                                  const std::chrono::milliseconds timeout, const std::stop_token &stopToken) noexcept
{
    if (!validSftpPath(remotePath))
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || m_sftp == nullptr || m_sftpFile != nullptr || !usesTransport(transport))
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
    auto *sftpHandle = static_cast<LIBSSH2_SFTP *>(m_sftp);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    LIBSSH2_SFTP_HANDLE *directory = nullptr;
    while (directory == nullptr)
    {
        directory = libssh2_sftp_open_ex(sftpHandle, remotePath.data(), static_cast<unsigned int>(remotePath.size()), 0,
                                         0, LIBSSH2_SFTP_OPENDIR);
        if (directory != nullptr)
        {
            break;
        }
        const int error = libssh2_session_last_errno(session);
        if (error != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapSftpError(session, sftpHandle, error));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }

    const auto closeDirectory = [&]() noexcept {
        while (directory != nullptr)
        {
            const int result = libssh2_sftp_close_handle(directory);
            if (result == 0)
            {
                directory = nullptr;
                return;
            }
            if (result != LIBSSH2_ERROR_EAGAIN)
            {
                return;
            }
            if (!waitForSessionIo(transport, session, deadline, {}).has_value())
            {
                return;
            }
        }
    };

    try
    {
        std::vector<sftp::DirectoryEntry> entries;
        std::array<char, 4096> nameBuffer{};
        while (true)
        {
            LIBSSH2_SFTP_ATTRIBUTES attributes{};
            const auto result =
                libssh2_sftp_readdir_ex(directory, nameBuffer.data(), nameBuffer.size(), nullptr, 0, &attributes);
            if (result > 0)
            {
                const std::string name(nameBuffer.data(), static_cast<std::size_t>(result));
                if (name == "." || name == "..")
                {
                    continue;
                }
                auto path = sftp::joinRemotePath(remotePath, name);
                if (!path)
                {
                    closeDirectory();
                    return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::ProtocolError});
                }
                entries.push_back(sftp::DirectoryEntry{
                    .name = name,
                    .remotePath = std::move(*path),
                    .type = entryType(attributes),
                    .size = (attributes.flags & LIBSSH2_SFTP_ATTR_SIZE) != 0 ? attributes.filesize : 0,
                    .modifiedUtcSeconds = (attributes.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) != 0
                                              ? std::optional<std::int64_t>{static_cast<std::int64_t>(attributes.mtime)}
                                              : std::nullopt,
                    .permissions = (attributes.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) != 0
                                       ? static_cast<std::uint32_t>(attributes.permissions)
                                       : 0,
                });
                continue;
            }
            if (result == 0)
            {
                closeDirectory();
                return entries;
            }
            if (result != LIBSSH2_ERROR_EAGAIN)
            {
                const SshTransportError error = mapSftpError(session, sftpHandle, static_cast<int>(result));
                closeDirectory();
                return std::unexpected(error);
            }
            auto ready = waitForSessionIo(transport, session, deadline, stopToken);
            if (!ready)
            {
                const SshTransportError error = ready.error();
                closeDirectory();
                return std::unexpected(error);
            }
        }
    }
    catch (const std::bad_alloc &)
    {
        closeDirectory();
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InitializationFailed});
    }
}

std::expected<std::string, SshTransportError>
Libssh2Session::canonicalizeSftpPath(SshByteTransport &transport, const std::string_view remotePath,
                                     const std::chrono::milliseconds timeout, const std::stop_token &stopToken) noexcept
{
    if (remotePath.empty() || remotePath.find('\0') != std::string_view::npos)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || m_sftp == nullptr || m_sftpFile != nullptr || !usesTransport(transport))
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidState});
    }
    if (timeout <= std::chrono::milliseconds::zero())
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::TimedOut});
    }

    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    auto *sftpHandle = static_cast<LIBSSH2_SFTP *>(m_sftp);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::array<char, 4096> resolved{};
    while (true)
    {
        const int result =
            libssh2_sftp_symlink_ex(sftpHandle, remotePath.data(), static_cast<unsigned int>(remotePath.size()),
                                    resolved.data(), static_cast<unsigned int>(resolved.size()), LIBSSH2_SFTP_REALPATH);
        if (result > 0)
        {
            auto normalized =
                sftp::normalizeRemotePath(std::string_view(resolved.data(), static_cast<std::size_t>(result)));
            if (!normalized)
            {
                return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::ProtocolError});
            }
            return std::move(*normalized);
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapSftpError(session, sftpHandle, result));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

std::expected<void, SshTransportError> Libssh2Session::createSftpDirectory(SshByteTransport &transport,
                                                                           const std::string_view remotePath,
                                                                           const std::chrono::milliseconds timeout,
                                                                           const std::stop_token &stopToken) noexcept
{
    if (!validSftpPath(remotePath))
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || m_sftp == nullptr || m_sftpFile != nullptr || !usesTransport(transport))
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
    auto *sftpHandle = static_cast<LIBSSH2_SFTP *>(m_sftp);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        const int result =
            libssh2_sftp_mkdir_ex(sftpHandle, remotePath.data(), static_cast<unsigned int>(remotePath.size()), 0755);
        if (result == 0)
        {
            return {};
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapSftpError(session, sftpHandle, result));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

std::expected<void, SshTransportError>
Libssh2Session::renameSftpEntry(SshByteTransport &transport, const std::string_view sourcePath,
                                const std::string_view destinationPath, const SftpRenameDisposition disposition,
                                const std::chrono::milliseconds timeout, const std::stop_token &stopToken) noexcept
{
    if (!validSftpPath(sourcePath) || !validSftpPath(destinationPath))
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || m_sftp == nullptr || m_sftpFile != nullptr || !usesTransport(transport))
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
    auto *sftpHandle = static_cast<LIBSSH2_SFTP *>(m_sftp);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        const long flags = disposition == SftpRenameDisposition::ReplaceAtomically
                               ? LIBSSH2_SFTP_RENAME_OVERWRITE | LIBSSH2_SFTP_RENAME_ATOMIC | LIBSSH2_SFTP_RENAME_NATIVE
                               : 0;
        const int result =
            libssh2_sftp_rename_ex(sftpHandle, sourcePath.data(), static_cast<unsigned int>(sourcePath.size()),
                                   destinationPath.data(), static_cast<unsigned int>(destinationPath.size()), flags);
        if (result == 0)
        {
            return {};
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapSftpError(session, sftpHandle, result));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

std::expected<void, SshTransportError> Libssh2Session::removeSftpFile(SshByteTransport &transport,
                                                                      const std::string_view remotePath,
                                                                      const std::chrono::milliseconds timeout,
                                                                      const std::stop_token &stopToken) noexcept
{
    if (!validSftpPath(remotePath))
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || m_sftp == nullptr || m_sftpFile != nullptr || !usesTransport(transport))
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
    auto *sftpHandle = static_cast<LIBSSH2_SFTP *>(m_sftp);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        const int result =
            libssh2_sftp_unlink_ex(sftpHandle, remotePath.data(), static_cast<unsigned int>(remotePath.size()));
        if (result == 0)
        {
            return {};
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapSftpError(session, sftpHandle, result));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

std::expected<void, SshTransportError> Libssh2Session::removeSftpDirectory(SshByteTransport &transport,
                                                                           const std::string_view remotePath,
                                                                           const std::chrono::milliseconds timeout,
                                                                           const std::stop_token &stopToken) noexcept
{
    if (!validSftpPath(remotePath))
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || m_sftp == nullptr || m_sftpFile != nullptr || !usesTransport(transport))
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
    auto *sftpHandle = static_cast<LIBSSH2_SFTP *>(m_sftp);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        const int result =
            libssh2_sftp_rmdir_ex(sftpHandle, remotePath.data(), static_cast<unsigned int>(remotePath.size()));
        if (result == 0)
        {
            return {};
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapSftpError(session, sftpHandle, result));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

std::expected<void, SshTransportError> Libssh2Session::openSftpFileForRead(SshByteTransport &transport,
                                                                           const std::string_view remotePath,
                                                                           const std::chrono::milliseconds timeout,
                                                                           const std::stop_token &stopToken) noexcept
{
    if (!validSftpPath(remotePath))
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || m_sftp == nullptr || m_sftpFile != nullptr || !usesTransport(transport))
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
    auto *sftpHandle = static_cast<LIBSSH2_SFTP *>(m_sftp);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        LIBSSH2_SFTP_HANDLE *file =
            libssh2_sftp_open_ex(sftpHandle, remotePath.data(), static_cast<unsigned int>(remotePath.size()),
                                 LIBSSH2_FXF_READ, 0, LIBSSH2_SFTP_OPENFILE);
        if (file != nullptr)
        {
            m_sftpFile = file;
            return {};
        }
        const int error = libssh2_session_last_errno(session);
        if (error != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapSftpError(session, sftpHandle, error));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

std::expected<void, SshTransportError> Libssh2Session::openSftpFileForWrite(SshByteTransport &transport,
                                                                            const std::string_view remotePath,
                                                                            const SftpWriteDisposition disposition,
                                                                            const std::chrono::milliseconds timeout,
                                                                            const std::stop_token &stopToken) noexcept
{
    if (!validSftpPath(remotePath))
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || m_sftp == nullptr || m_sftpFile != nullptr || !usesTransport(transport))
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

    const unsigned long flags =
        disposition == SftpWriteDisposition::CreateNew      ? LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_EXCL
        : disposition == SftpWriteDisposition::OpenOrCreate ? LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT
                                                            : LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC;
    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    auto *sftpHandle = static_cast<LIBSSH2_SFTP *>(m_sftp);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        LIBSSH2_SFTP_HANDLE *file =
            libssh2_sftp_open_ex(sftpHandle, remotePath.data(), static_cast<unsigned int>(remotePath.size()), flags,
                                 0644, LIBSSH2_SFTP_OPENFILE);
        if (file != nullptr)
        {
            m_sftpFile = file;
            return {};
        }
        const int error = libssh2_session_last_errno(session);
        if (error != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapSftpError(session, sftpHandle, error));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

std::expected<std::size_t, SshTransportError> Libssh2Session::readSftpFile(SshByteTransport &transport,
                                                                           const std::span<char> output,
                                                                           const std::chrono::milliseconds timeout,
                                                                           const std::stop_token &stopToken) noexcept
{
    if (output.empty())
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || m_sftp == nullptr || m_sftpFile == nullptr || !usesTransport(transport))
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
    auto *sftpHandle = static_cast<LIBSSH2_SFTP *>(m_sftp);
    auto *file = static_cast<LIBSSH2_SFTP_HANDLE *>(m_sftpFile);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        const auto result = libssh2_sftp_read(file, output.data(), output.size());
        if (result >= 0)
        {
            return static_cast<std::size_t>(result);
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapSftpError(session, sftpHandle, static_cast<int>(result)));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

std::expected<void, SshTransportError> Libssh2Session::writeSftpFile(SshByteTransport &transport,
                                                                     const std::span<const char> input,
                                                                     const std::chrono::milliseconds timeout,
                                                                     const std::stop_token &stopToken) noexcept
{
    if (input.empty())
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || m_sftp == nullptr || m_sftpFile == nullptr || !usesTransport(transport))
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
    auto *sftpHandle = static_cast<LIBSSH2_SFTP *>(m_sftp);
    auto *file = static_cast<LIBSSH2_SFTP_HANDLE *>(m_sftpFile);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::size_t written = 0;
    while (written < input.size())
    {
        const auto result = libssh2_sftp_write(file, input.data() + written, input.size() - written);
        if (result > 0)
        {
            written += static_cast<std::size_t>(result);
            continue;
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapSftpError(session, sftpHandle, static_cast<int>(result)));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
    return {};
}

std::expected<void, SshTransportError> Libssh2Session::closeSftpFile(SshByteTransport &transport,
                                                                     const std::chrono::milliseconds timeout,
                                                                     const std::stop_token &stopToken) noexcept
{
    if (m_session == nullptr || m_sftp == nullptr || m_sftpFile == nullptr || !usesTransport(transport))
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
    auto *sftpHandle = static_cast<LIBSSH2_SFTP *>(m_sftp);
    auto *file = static_cast<LIBSSH2_SFTP_HANDLE *>(m_sftpFile);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        const int result = libssh2_sftp_close_handle(file);
        if (result == 0)
        {
            m_sftpFile = nullptr;
            return {};
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapSftpError(session, sftpHandle, result));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

std::expected<void, SshTransportError> Libssh2Session::seekSftpFile(const std::uint64_t offset) noexcept
{
    if (m_session == nullptr || m_sftp == nullptr || m_sftpFile == nullptr)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidState});
    }
    libssh2_sftp_seek64(static_cast<LIBSSH2_SFTP_HANDLE *>(m_sftpFile), offset);
    return {};
}

bool Libssh2Session::sftpFileOpen() const noexcept
{
    return m_sftpFile != nullptr;
}

std::expected<void, SshTransportError> Libssh2Session::closeSftp(SshByteTransport &transport,
                                                                 const std::chrono::milliseconds timeout,
                                                                 const std::stop_token &stopToken) noexcept
{
    if (m_session == nullptr || m_sftp == nullptr || m_sftpFile != nullptr || !usesTransport(transport))
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
    auto *sftpHandle = static_cast<LIBSSH2_SFTP *>(m_sftp);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        const int result = libssh2_sftp_shutdown(sftpHandle);
        if (result == 0)
        {
            m_sftp = nullptr;
            return {};
        }
        if (result != LIBSSH2_ERROR_EAGAIN)
        {
            return std::unexpected(mapSftpError(session, sftpHandle, result));
        }
        auto ready = waitForSessionIo(transport, session, deadline, stopToken);
        if (!ready)
        {
            return std::unexpected(ready.error());
        }
    }
}

bool Libssh2Session::sftpOpen() const noexcept
{
    return m_sftp != nullptr;
}

std::expected<void, SshTransportError> Libssh2Session::startAuxiliaryCommand(const std::string_view command) noexcept
{
    constexpr std::size_t maximumCommandBytes = std::size_t{16} * 1024;
    if (command.empty() || command.size() > maximumCommandBytes || command.find('\0') != std::string_view::npos)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || !m_authenticated || m_auxiliaryPhase != AuxiliaryCommandPhase::Idle
        || m_auxiliaryChannel != nullptr || m_sftp != nullptr)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidState});
    }

    try
    {
        m_auxiliaryCommand.assign(command);
    }
    catch (const std::bad_alloc &)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InitializationFailed});
    }
    m_auxiliaryExitStatus = 0;
    m_auxiliaryPhase = AuxiliaryCommandPhase::Opening;
    return {};
}

std::expected<AuxiliaryCommandPollResult, SshTransportError>
Libssh2Session::pollAuxiliaryCommand(const std::span<char> output) noexcept
{
    if (output.empty())
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidArgument});
    }
    if (m_session == nullptr || m_auxiliaryPhase == AuxiliaryCommandPhase::Idle)
    {
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidState});
    }

    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    if (m_auxiliaryPhase == AuxiliaryCommandPhase::Opening)
    {
        LIBSSH2_CHANNEL *channel = libssh2_channel_open_session(session);
        if (channel == nullptr)
        {
            const int error = libssh2_session_last_errno(session);
            if (error == LIBSSH2_ERROR_EAGAIN)
            {
                return AuxiliaryCommandPollResult{};
            }
            m_auxiliaryCommand.clear();
            m_auxiliaryPhase = AuxiliaryCommandPhase::Idle;
            return std::unexpected(mapChannelError(error));
        }
        m_auxiliaryChannel = channel;
        m_auxiliaryPhase = AuxiliaryCommandPhase::Requesting;
    }

    auto *channel = static_cast<LIBSSH2_CHANNEL *>(m_auxiliaryChannel);
    if (channel == nullptr)
    {
        m_auxiliaryCommand.clear();
        m_auxiliaryPhase = AuxiliaryCommandPhase::Idle;
        return std::unexpected(SshTransportError{.kind = SshTransportErrorKind::InvalidState});
    }

    if (m_auxiliaryPhase == AuxiliaryCommandPhase::Requesting)
    {
        const int result = libssh2_channel_process_startup(channel, "exec", 4U, m_auxiliaryCommand.data(),
                                                           static_cast<unsigned int>(m_auxiliaryCommand.size()));
        if (result == LIBSSH2_ERROR_EAGAIN)
        {
            return AuxiliaryCommandPollResult{};
        }
        if (result != 0)
        {
            m_auxiliaryPhase = AuxiliaryCommandPhase::Closing;
            return std::unexpected(mapChannelError(result));
        }
        m_auxiliaryCommand.clear();
        m_auxiliaryPhase = AuxiliaryCommandPhase::Reading;
    }

    if (m_auxiliaryPhase == AuxiliaryCommandPhase::Reading)
    {
        const auto read = libssh2_channel_read(channel, output.data(), output.size());
        if (read > 0)
        {
            return AuxiliaryCommandPollResult{
                .progress = AuxiliaryCommandProgress::Output,
                .bytesRead = static_cast<std::size_t>(read),
            };
        }
        if (read < 0 && read != LIBSSH2_ERROR_EAGAIN)
        {
            m_auxiliaryPhase = AuxiliaryCommandPhase::Closing;
            return std::unexpected(mapChannelError(static_cast<int>(read)));
        }

        // Auxiliary diagnostics are deliberately discarded: commands are
        // fixed by ztermy and raw remote output must not enter application logs.
        std::array<char, 4096> errorBuffer{};
        const auto errorRead = libssh2_channel_read_stderr(channel, errorBuffer.data(), errorBuffer.size());
        if (errorRead < 0 && errorRead != LIBSSH2_ERROR_EAGAIN)
        {
            m_auxiliaryPhase = AuxiliaryCommandPhase::Closing;
            return std::unexpected(mapChannelError(static_cast<int>(errorRead)));
        }
        if (libssh2_channel_eof(channel) == 0)
        {
            return AuxiliaryCommandPollResult{};
        }
        m_auxiliaryPhase = AuxiliaryCommandPhase::Closing;
    }

    if (m_auxiliaryPhase == AuxiliaryCommandPhase::Closing)
    {
        const int result = libssh2_channel_close(channel);
        if (result == LIBSSH2_ERROR_EAGAIN)
        {
            return AuxiliaryCommandPollResult{};
        }
        m_auxiliaryExitStatus = libssh2_channel_get_exit_status(channel);
        m_auxiliaryPhase = AuxiliaryCommandPhase::Freeing;
        if (result != 0)
        {
            return std::unexpected(mapChannelError(result));
        }
    }

    const int freeResult = libssh2_channel_free(channel);
    if (freeResult == LIBSSH2_ERROR_EAGAIN)
    {
        return AuxiliaryCommandPollResult{};
    }
    if (freeResult != 0)
    {
        return std::unexpected(mapChannelError(freeResult));
    }

    const int exitStatus = m_auxiliaryExitStatus;
    m_auxiliaryChannel = nullptr;
    m_auxiliaryCommand.clear();
    m_auxiliaryPhase = AuxiliaryCommandPhase::Idle;
    m_auxiliaryExitStatus = 0;
    return AuxiliaryCommandPollResult{
        .progress = AuxiliaryCommandProgress::Completed,
        .exitStatus = exitStatus,
    };
}

void Libssh2Session::cancelAuxiliaryCommand() noexcept
{
    if (m_auxiliaryPhase == AuxiliaryCommandPhase::Opening && m_auxiliaryChannel == nullptr)
    {
        m_auxiliaryCommand.clear();
        m_auxiliaryPhase = AuxiliaryCommandPhase::Idle;
        return;
    }
    if (m_auxiliaryPhase == AuxiliaryCommandPhase::Requesting || m_auxiliaryPhase == AuxiliaryCommandPhase::Reading)
    {
        m_auxiliaryCommand.clear();
        m_auxiliaryPhase = AuxiliaryCommandPhase::Closing;
    }
}

bool Libssh2Session::auxiliaryCommandActive() const noexcept
{
    return m_auxiliaryPhase != AuxiliaryCommandPhase::Idle;
}

} // namespace ztermy::ssh
