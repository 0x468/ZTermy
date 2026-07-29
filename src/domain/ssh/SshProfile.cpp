#include "domain/ssh/SshProfile.h"

namespace
{

constexpr std::size_t maximumIdLength = 128;
constexpr std::size_t maximumNameLength = 256;
constexpr std::size_t maximumHostLength = 1024;
constexpr std::size_t maximumUsernameLength = 256;
constexpr std::size_t maximumPrivateKeyPathLength = 32767;

[[nodiscard]] bool nonEmptyWithin(const std::string &value, const std::size_t maximumLength) noexcept
{
    return !value.empty() && value.size() <= maximumLength;
}

} // namespace

namespace ztermy::ssh
{

bool validSshProfile(const SshProfile &profile) noexcept
{
    if (!nonEmptyWithin(profile.id, maximumIdLength) || !nonEmptyWithin(profile.name, maximumNameLength)
        || !nonEmptyWithin(profile.host, maximumHostLength) || !nonEmptyWithin(profile.username, maximumUsernameLength)
        || profile.port == 0)
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
