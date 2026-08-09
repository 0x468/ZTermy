#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ztermy::ssh
{

enum class SshFailureKind : std::uint8_t;

enum class SshAuthenticationMethod : std::uint8_t
{
    PrivateKey,
    Password,
    Agent,
};

enum class SshStartupCommandMode : std::uint8_t
{
    Paste,
    LineDelay,
};

enum class SshReconnectPolicy : std::uint8_t
{
    Never,
    OnTransportFailure,
};

struct SshEnvironmentVariable final
{
    std::string name;
    std::string value;

    friend bool operator==(const SshEnvironmentVariable &, const SshEnvironmentVariable &) = default;
};

struct SshSessionOptions final
{
    std::string terminalType = "xterm-256color";
    std::uint16_t keepaliveIntervalSeconds = 0;
    std::uint8_t keepaliveFailureThreshold = 3;
    std::string startupCommand;
    SshStartupCommandMode startupCommandMode = SshStartupCommandMode::Paste;
    std::uint16_t startupLineDelayMilliseconds = 100;
    std::vector<SshEnvironmentVariable> environment;
    SshReconnectPolicy reconnectPolicy = SshReconnectPolicy::Never;
    std::uint8_t reconnectMaximumAttempts = 3;
    std::uint16_t reconnectInitialBackoffMilliseconds = 1000;

    friend bool operator==(const SshSessionOptions &, const SshSessionOptions &) = default;
};

struct SshKeywordHighlightRule final
{
    std::string id;
    std::string pattern;
    std::string foreground;
    std::string background;
    bool enabled = true;
    bool caseSensitive = false;

    friend bool operator==(const SshKeywordHighlightRule &, const SshKeywordHighlightRule &) = default;
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
    std::optional<std::string> credentialReference;
    std::optional<std::int64_t> lastConnectedUtcMs;
    std::vector<SshKeywordHighlightRule> keywordHighlightRules;
    bool keywordHighlightEnabled = true;
    SshSessionOptions sessionOptions;

    friend bool operator==(const SshProfile &, const SshProfile &) = default;
};

[[nodiscard]] bool validSshProfile(const SshProfile &profile) noexcept;
[[nodiscard]] bool validKeywordHighlightRule(const SshKeywordHighlightRule &rule) noexcept;
[[nodiscard]] bool validSshSessionOptions(const SshSessionOptions &options) noexcept;
[[nodiscard]] bool shouldReconnectAfter(SshReconnectPolicy policy, SshFailureKind failure) noexcept;
[[nodiscard]] std::uint32_t reconnectBackoffMilliseconds(const SshSessionOptions &options,
                                                         std::uint8_t attempt) noexcept;

} // namespace ztermy::ssh
