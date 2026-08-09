#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ztermy::ssh
{

enum class SshAuthenticationMethod : std::uint8_t
{
    PrivateKey,
    Password,
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

    friend bool operator==(const SshProfile &, const SshProfile &) = default;
};

[[nodiscard]] bool validSshProfile(const SshProfile &profile) noexcept;
[[nodiscard]] bool validKeywordHighlightRule(const SshKeywordHighlightRule &rule) noexcept;

} // namespace ztermy::ssh
