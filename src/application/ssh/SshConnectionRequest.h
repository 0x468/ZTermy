#pragma once

#include "core/security/SensitiveByteArray.h"
#include "domain/ssh/SshProfile.h"

#include <QString>

#include <cstdint>

namespace ztermy::ssh
{

struct SshConnectionRequest final
{
    QString host;
    std::uint16_t port = 22;
    QString username;
    SshAuthenticationMethod authentication = SshAuthenticationMethod::PrivateKey;
    QString privateKeyPath;
    security::SensitiveByteArray secret;
    QString knownHostsPath;
};

[[nodiscard]] bool validSshConnectionRequest(const SshConnectionRequest &request) noexcept;

} // namespace ztermy::ssh
