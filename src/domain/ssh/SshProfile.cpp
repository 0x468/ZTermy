#include "domain/ssh/SshProfile.h"

#include <algorithm>
#include <cctype>

namespace
{

constexpr std::size_t maximumIdLength = 128;
constexpr std::size_t maximumNameLength = 256;
constexpr std::size_t maximumGroupLength = 128;
constexpr std::size_t maximumHostLength = 1024;
constexpr std::size_t maximumUsernameLength = 256;
constexpr std::size_t maximumPrivateKeyPathLength = 32767;

[[nodiscard]] bool nonEmptyWithin(const std::string &value, const std::size_t maximumLength) noexcept
{
    return !value.empty() && value.size() <= maximumLength;
}

[[nodiscard]] bool validCredentialReference(const std::optional<std::string> &reference) noexcept
{
    return !reference
           || (nonEmptyWithin(*reference, maximumIdLength)
               && std::ranges::all_of(*reference, [](const unsigned char character) {
                      return std::isalnum(character) != 0 || character == '-' || character == '_';
                  }));
}

} // namespace

namespace ztermy::ssh
{

bool validSshProfile(const SshProfile &profile) noexcept
{
    if (!nonEmptyWithin(profile.id, maximumIdLength) || !nonEmptyWithin(profile.name, maximumNameLength)
        || profile.group.size() > maximumGroupLength || !nonEmptyWithin(profile.host, maximumHostLength)
        || !nonEmptyWithin(profile.username, maximumUsernameLength) || profile.port == 0
        || !validCredentialReference(profile.credentialReference)
        || (profile.lastConnectedUtcMs.has_value() && *profile.lastConnectedUtcMs < 0))
    {
        return false;
    }

    switch (profile.authentication)
    {
        case SshAuthenticationMethod::PrivateKey:
            return nonEmptyWithin(profile.privateKeyPath, maximumPrivateKeyPathLength);
        case SshAuthenticationMethod::Password:
            return profile.privateKeyPath.empty() && !profile.privateKeyPassphraseRequired;
    }
    return false;
}

} // namespace ztermy::ssh
