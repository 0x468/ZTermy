#pragma once

#include "core/security/SensitiveByteArray.h"
#include "domain/ssh/SshProfile.h"

#include <QString>

#include <cstdint>
#include <vector>

namespace ztermy::ssh
{

struct SshJumpHostRequest final
{
    QString profileId;
    QString displayName;
    QString host;
    std::uint16_t port = 22;
    QString username;
    SshAuthenticationMethod authentication = SshAuthenticationMethod::PrivateKey;
    QString privateKeyPath;
    security::SensitiveByteArray secret;
    SshProxyOptions proxy;
    security::SensitiveByteArray proxySecret;
};

struct SshConnectionRequest final
{
    QString host;
    std::uint16_t port = 22;
    QString username;
    SshAuthenticationMethod authentication = SshAuthenticationMethod::PrivateKey;
    QString privateKeyPath;
    security::SensitiveByteArray secret;
    SshProxyOptions proxy;
    security::SensitiveByteArray proxySecret;
    std::vector<SshJumpHostRequest> jumpHosts;
    QString knownHostsPath;
    SshSessionOptions sessionOptions;
};

[[nodiscard]] bool validSshConnectionRequest(const SshConnectionRequest &request) noexcept;

} // namespace ztermy::ssh
