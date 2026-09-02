#pragma once

#include "domain/ssh/SshKeychain.h"
#include "domain/ssh/SshProfile.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace ztermy::ssh
{

enum class SshIdentityResolutionError : std::uint8_t
{
    MissingIdentity,
    MissingKey,
    InvalidIdentity,
};

struct ResolvedSshIdentity final
{
    std::string username;
    SshAuthenticationMethod authentication = SshAuthenticationMethod::PrivateKey;
    std::string privateKeyPath;
    std::string publicKeyPath;
    bool credentialRequired = false;
    std::optional<std::string> credentialReference;

    friend bool operator==(const ResolvedSshIdentity &, const ResolvedSshIdentity &) = default;
};

[[nodiscard]] std::expected<ResolvedSshIdentity, SshIdentityResolutionError>
resolveSshIdentity(const SshProfile &profile, const SshKeychainCatalog &catalog);

} // namespace ztermy::ssh
