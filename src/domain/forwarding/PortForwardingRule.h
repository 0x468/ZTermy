#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::forwarding
{

inline constexpr std::size_t maximumPortForwardingRuleCount = 128;
inline constexpr std::size_t maximumActivePortForwardingRuleCount = 16;
inline constexpr std::size_t maximumPortForwardingClientsPerRule = 32;

enum class PortForwardingType : std::uint8_t
{
    Local,
    Remote,
    Dynamic,
};

struct PortForwardingEndpoint final
{
    std::string host;
    std::uint16_t port = 0;

    friend bool operator==(const PortForwardingEndpoint &, const PortForwardingEndpoint &) = default;
};

struct PortForwardingRule final
{
    std::string id;
    std::string label;
    std::string profileId;
    PortForwardingType type = PortForwardingType::Local;
    PortForwardingEndpoint bind;
    PortForwardingEndpoint destination;
    bool autoStart = false;

    friend bool operator==(const PortForwardingRule &, const PortForwardingRule &) = default;
};

[[nodiscard]] bool validPortForwardingRule(const PortForwardingRule &rule) noexcept;
[[nodiscard]] bool validPortForwardingRules(std::span<const PortForwardingRule> rules) noexcept;
[[nodiscard]] bool portForwardingRulesReferenceProfile(std::span<const PortForwardingRule> rules,
                                                       std::string_view profileId) noexcept;
[[nodiscard]] std::vector<PortForwardingRule>::const_iterator
findPortForwardingRule(const std::vector<PortForwardingRule> &rules, std::string_view id) noexcept;

} // namespace ztermy::forwarding
