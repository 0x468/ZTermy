#pragma once

#include <libssh2.h>
#include <libssh2_sftp.h>
#include "infrastructure/ssh/Libssh2Session.h"

namespace ztermy::ssh::detail
{
[[nodiscard]] ztermy::ssh::SshTransportError mapHandshakeError(const int error) noexcept;

[[nodiscard]] ztermy::ssh::SshTransportError mapAuthenticationError(const int error) noexcept;

[[nodiscard]] ztermy::ssh::SshTransportError mapPrivateKeyAuthenticationError(const int error) noexcept;

[[nodiscard]] ztermy::ssh::SshTransportError mapChannelError(const int error) noexcept;

[[nodiscard]] ztermy::ssh::SshTransportError mapSftpError(LIBSSH2_SESSION *session, LIBSSH2_SFTP *sftp,
                                                          const int error) noexcept;
} // namespace ztermy::ssh::detail
