#pragma once

#include <cstdint>
#include <string>

namespace ztermy::ssh
{

enum class SshAuthenticationMethod : std::uint8_t
{
    PrivateKey,
    Password,
};

struct SshProfile
{
    std::string id;
    std::string name;
    std::string group;
    std::string host;
    std::uint16_t port = 22;
    std::string username;
    SshAuthenticationMethod authentication = SshAuthenticationMethod::PrivateKey;
    std::string privateKeyPath;
    bool privateKeyPassphraseRequired = false;

    friend bool operator==(const SshProfile &, const SshProfile &) = default;
};

[[nodiscard]] bool validSshProfile(const SshProfile &profile) noexcept;

} // namespace ztermy::ssh
