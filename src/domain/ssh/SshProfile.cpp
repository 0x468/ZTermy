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
constexpr std::size_t maximumKeywordRuleCount = 16;

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

[[nodiscard]] bool validColor(const std::string &value) noexcept
{
    if (value.empty())
    {
        return true;
    }
    if ((value.size() != 7 && value.size() != 9) || value.front() != '#')
    {
        return false;
    }
    return std::ranges::all_of(value.begin() + 1, value.end(), [](const unsigned char character) {
        return std::isxdigit(character) != 0;
    });
}

} // namespace

namespace ztermy::ssh
{

bool validKeywordHighlightRule(const SshKeywordHighlightRule &rule) noexcept
{
    return nonEmptyWithin(rule.id, 64) && nonEmptyWithin(rule.pattern, 128) && validColor(rule.foreground)
           && validColor(rule.background) && (!rule.foreground.empty() || !rule.background.empty());
}

bool validSshProfile(const SshProfile &profile) noexcept
{
    if (!nonEmptyWithin(profile.id, maximumIdLength) || !nonEmptyWithin(profile.name, maximumNameLength)
        || profile.group.size() > maximumGroupLength || !nonEmptyWithin(profile.host, maximumHostLength)
        || !nonEmptyWithin(profile.username, maximumUsernameLength) || profile.port == 0
        || !validCredentialReference(profile.credentialReference)
        || profile.keywordHighlightRules.size() > maximumKeywordRuleCount
        || !std::ranges::all_of(profile.keywordHighlightRules, validKeywordHighlightRule)
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
