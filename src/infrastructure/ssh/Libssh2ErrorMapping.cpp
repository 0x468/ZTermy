#include "infrastructure/ssh/Libssh2ErrorMapping.h"

namespace ztermy::ssh::detail
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

} // namespace ztermy::ssh::detail
