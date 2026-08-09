#include "domain/ssh/SshProfile.h"

#include "domain/ssh/SshConnectionState.h"

#include <algorithm>
#include <cctype>
#include <cstddef>

namespace
{

constexpr std::size_t maximumIdLength = 128;
constexpr std::size_t maximumNameLength = 256;
constexpr std::size_t maximumGroupLength = 128;
constexpr std::size_t maximumHostLength = 1024;
constexpr std::size_t maximumUsernameLength = 256;
constexpr std::size_t maximumProxyHostLength = 1024;
constexpr std::size_t maximumProxyUsernameLength = 255;
constexpr std::size_t maximumPrivateKeyPathLength = 32767;
constexpr std::size_t maximumKeywordRuleCount = 16;
constexpr std::size_t maximumEnvironmentVariableCount = 32;
constexpr std::size_t maximumEnvironmentNameLength = 128;
constexpr std::size_t maximumEnvironmentValueLength = 4096;
constexpr std::size_t maximumStartupCommandLength = 32768;
constexpr std::size_t maximumStartupCommandLineBreaks = 255;

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

[[nodiscard]] bool validBoundedText(const std::string &value, const std::size_t maximumLength) noexcept
{
    return value.size() <= maximumLength && value.find('\0') == std::string::npos;
}

[[nodiscard]] bool validTerminalType(const std::string &value) noexcept
{
    return nonEmptyWithin(value, 64) && std::ranges::all_of(value, [](const unsigned char character) {
               return std::isalnum(character) != 0 || character == '-' || character == '_' || character == '.'
                      || character == '+';
           });
}

[[nodiscard]] bool validEnvironmentName(const std::string &value) noexcept
{
    if (!nonEmptyWithin(value, maximumEnvironmentNameLength)
        || !(std::isalpha(static_cast<unsigned char>(value.front())) != 0 || value.front() == '_'))
    {
        return false;
    }
    return std::ranges::all_of(value.begin() + 1, value.end(), [](const unsigned char character) {
        return std::isalnum(character) != 0 || character == '_';
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

bool validSshSessionOptions(const SshSessionOptions &options) noexcept
{
    if (!validTerminalType(options.terminalType) || options.keepaliveIntervalSeconds > 3600
        || options.keepaliveFailureThreshold == 0 || options.keepaliveFailureThreshold > 10
        || !validBoundedText(options.startupCommand, maximumStartupCommandLength)
        || std::cmp_greater(std::ranges::count_if(options.startupCommand,
                                                  [](const char character) {
                                                      return character == '\r' || character == '\n';
                                                  }),
                            maximumStartupCommandLineBreaks)
        || (options.startupCommandMode != SshStartupCommandMode::Paste
            && options.startupCommandMode != SshStartupCommandMode::LineDelay)
        || options.startupLineDelayMilliseconds > 5000 || options.environment.size() > maximumEnvironmentVariableCount
        || options.reconnectMaximumAttempts == 0 || options.reconnectMaximumAttempts > 10
        || options.reconnectInitialBackoffMilliseconds < 250 || options.reconnectInitialBackoffMilliseconds > 30000
        || (options.reconnectPolicy != SshReconnectPolicy::Never
            && options.reconnectPolicy != SshReconnectPolicy::OnTransportFailure))
    {
        return false;
    }

    for (std::size_t index = 0; index < options.environment.size(); ++index)
    {
        const SshEnvironmentVariable &variable = options.environment[index];
        if (!validEnvironmentName(variable.name) || !validBoundedText(variable.value, maximumEnvironmentValueLength))
        {
            return false;
        }
        const auto duplicate =
            std::ranges::find(options.environment.begin() + static_cast<std::ptrdiff_t>(index + 1),
                              options.environment.end(), variable.name, &SshEnvironmentVariable::name);
        if (duplicate != options.environment.end())
        {
            return false;
        }
    }
    return true;
}

bool validSshProxyOptions(const SshProxyOptions &options) noexcept
{
    switch (options.type)
    {
        case SshProxyType::None:
            return options.host.empty() && options.port == 0 && options.username.empty()
                   && !options.credentialReference.has_value();
        case SshProxyType::Socks5:
        case SshProxyType::HttpConnect:
            return nonEmptyWithin(options.host, maximumProxyHostLength) && options.port != 0
                   && options.username.size() <= maximumProxyUsernameLength
                   && validCredentialReference(options.credentialReference)
                   && (!options.username.empty() || !options.credentialReference.has_value());
    }
    return false;
}

bool shouldReconnectAfter(const SshReconnectPolicy policy, const SshFailureKind failure) noexcept
{
    if (policy != SshReconnectPolicy::OnTransportFailure)
    {
        return false;
    }
    switch (failure)
    {
        case SshFailureKind::NameResolutionFailed:
        case SshFailureKind::ConnectionRefused:
        case SshFailureKind::TimedOut:
        case SshFailureKind::TransportError:
        case SshFailureKind::RemoteClosed:
            return true;
        case SshFailureKind::HostKeyChanged:
        case SshFailureKind::HostKeyInvalid:
        case SshFailureKind::AuthenticationRejected:
        case SshFailureKind::AuthenticationUnavailable:
        case SshFailureKind::ChannelOpenFailed:
        case SshFailureKind::Cancelled:
        case SshFailureKind::ProtocolError:
            return false;
    }
    return false;
}

std::uint32_t reconnectBackoffMilliseconds(const SshSessionOptions &options, const std::uint8_t attempt) noexcept
{
    if (attempt == 0)
    {
        return 0;
    }
    constexpr std::uint32_t maximumBackoffMilliseconds = 30'000;
    std::uint32_t delay = options.reconnectInitialBackoffMilliseconds;
    for (std::uint8_t index = 1; index < attempt && delay < maximumBackoffMilliseconds; ++index)
    {
        delay = (std::min)(delay * 2U, maximumBackoffMilliseconds);
    }
    return delay;
}

bool validSshProfile(const SshProfile &profile) noexcept
{
    if (!nonEmptyWithin(profile.id, maximumIdLength) || !nonEmptyWithin(profile.name, maximumNameLength)
        || profile.group.size() > maximumGroupLength || !nonEmptyWithin(profile.host, maximumHostLength)
        || !nonEmptyWithin(profile.username, maximumUsernameLength) || profile.port == 0
        || !validCredentialReference(profile.credentialReference)
        || profile.keywordHighlightRules.size() > maximumKeywordRuleCount
        || !std::ranges::all_of(profile.keywordHighlightRules, validKeywordHighlightRule)
        || (profile.lastConnectedUtcMs.has_value() && *profile.lastConnectedUtcMs < 0)
        || !validSshSessionOptions(profile.sessionOptions) || !validSshProxyOptions(profile.proxy))
    {
        return false;
    }

    switch (profile.authentication)
    {
        case SshAuthenticationMethod::PrivateKey:
            return nonEmptyWithin(profile.privateKeyPath, maximumPrivateKeyPathLength);
        case SshAuthenticationMethod::Password:
            return profile.privateKeyPath.empty() && !profile.privateKeyPassphraseRequired;
        case SshAuthenticationMethod::Agent:
            return profile.privateKeyPath.empty() && !profile.privateKeyPassphraseRequired
                   && !profile.credentialReference.has_value();
    }
    return false;
}

} // namespace ztermy::ssh
