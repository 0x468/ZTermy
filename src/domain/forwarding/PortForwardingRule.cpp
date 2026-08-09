#include "domain/forwarding/PortForwardingRule.h"

#include <algorithm>
#include <ranges>

namespace ztermy::forwarding
{
namespace
{

[[nodiscard]] bool validText(const std::string_view value, const std::size_t maximumBytes) noexcept
{
    return !value.empty() && value.size() <= maximumBytes
           && std::ranges::none_of(value, [](const unsigned char character) {
                  return character < 0x20U || character == 0x7FU;
              });
}

[[nodiscard]] bool validEndpoint(const PortForwardingEndpoint &endpoint) noexcept
{
    return validText(endpoint.host, 255) && endpoint.port != 0
           && std::ranges::none_of(endpoint.host, [](const unsigned char character) {
                  return character == ' ' || character == '\t';
              });
}

} // namespace

bool validPortForwardingRule(const PortForwardingRule &rule) noexcept
{
    if (!validText(rule.id, 128) || !validText(rule.label, 128) || !validText(rule.profileId, 128)
        || !validEndpoint(rule.bind))
    {
        return false;
    }

    if (rule.type == PortForwardingType::Dynamic)
    {
        return rule.destination.host.empty() && rule.destination.port == 0;
    }
    return validEndpoint(rule.destination);
}

bool validPortForwardingRules(const std::span<const PortForwardingRule> rules) noexcept
{
    if (rules.size() > maximumPortForwardingRuleCount)
    {
        return false;
    }

    for (std::size_t index = 0; index < rules.size(); ++index)
    {
        if (!validPortForwardingRule(rules[index]))
        {
            return false;
        }
        const auto duplicate = std::ranges::find(rules.subspan(index + 1), rules[index].id, &PortForwardingRule::id);
        if (duplicate != rules.subspan(index + 1).end())
        {
            return false;
        }
    }
    return true;
}

bool portForwardingRulesReferenceProfile(const std::span<const PortForwardingRule> rules,
                                         const std::string_view profileId) noexcept
{
    return std::ranges::any_of(rules, [profileId](const PortForwardingRule &rule) {
        return rule.profileId == profileId;
    });
}

std::vector<PortForwardingRule>::const_iterator findPortForwardingRule(const std::vector<PortForwardingRule> &rules,
                                                                       const std::string_view id) noexcept
{
    return std::ranges::find(rules, id, &PortForwardingRule::id);
}

} // namespace ztermy::forwarding
