#include "domain/ai/AiPermissionPolicy.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <regex>
#include <unordered_set>
#include <utility>

namespace ztermy::ai
{
namespace
{

[[nodiscard]] std::string normalizedCommand(const std::string_view command)
{
    constexpr std::size_t maximumInspectedBytes = std::size_t{16} * 1024;
    std::string result;
    result.reserve(std::min(command.size(), maximumInspectedBytes));
    bool previousSpace = true;
    for (const unsigned char value : command.substr(0, maximumInspectedBytes))
    {
        if (std::isspace(value) != 0)
        {
            if (!previousSpace)
            {
                result.push_back(' ');
                previousSpace = true;
            }
            continue;
        }
        result.push_back(static_cast<char>(std::tolower(value)));
        previousSpace = false;
    }
    if (!result.empty() && result.back() == ' ')
    {
        result.pop_back();
    }
    return result;
}

template <std::size_t Size>
[[nodiscard]] bool containsAny(const std::string_view command, const std::array<std::string_view, Size> &patterns)
{
    return std::ranges::any_of(patterns, [command](const std::string_view pattern) {
        return command.contains(pattern);
    });
}

[[nodiscard]] AiCommandRiskReport risk(const AiCommandRiskCategory category, std::string reason)
{
    return {.category = category, .reason = std::move(reason)};
}

[[nodiscard]] bool globMatches(const std::string_view pattern, const std::string_view value) noexcept
{
    std::size_t patternIndex = 0;
    std::size_t valueIndex = 0;
    std::size_t starIndex = std::string_view::npos;
    std::size_t starValueIndex = 0;
    while (valueIndex < value.size())
    {
        if (patternIndex < pattern.size()
            && (pattern[patternIndex] == '?' || pattern[patternIndex] == value[valueIndex]))
        {
            ++patternIndex;
            ++valueIndex;
            continue;
        }
        if (patternIndex < pattern.size() && pattern[patternIndex] == '*')
        {
            starIndex = patternIndex++;
            starValueIndex = valueIndex;
            continue;
        }
        if (starIndex == std::string_view::npos)
        {
            return false;
        }
        patternIndex = starIndex + 1;
        valueIndex = ++starValueIndex;
    }
    while (patternIndex < pattern.size() && pattern[patternIndex] == '*')
    {
        ++patternIndex;
    }
    return patternIndex == pattern.size();
}

[[nodiscard]] bool subjectMatches(const AiPermissionRule &rule, const std::string_view subject)
{
    switch (rule.matcher)
    {
        case AiPermissionRuleMatcher::exact:
            return subject == rule.pattern;
        case AiPermissionRuleMatcher::prefix:
            return subject.starts_with(rule.pattern);
        case AiPermissionRuleMatcher::glob:
            return globMatches(rule.pattern, subject);
        case AiPermissionRuleMatcher::regex:
            try
            {
                return std::regex_match(
                    subject.begin(), subject.end(),
                    std::regex(rule.pattern, std::regex_constants::ECMAScript | std::regex_constants::optimize));
            }
            catch (const std::regex_error &)
            {
                return false;
            }
        case AiPermissionRuleMatcher::all:
            return true;
    }
    return false;
}

[[nodiscard]] bool scopeMatches(const AiPermissionRule &rule, const AiPermissionRuleQuery &query) noexcept
{
    switch (rule.duration)
    {
        case AiPermissionRuleDuration::once:
        case AiPermissionRuleDuration::session:
            return !query.sessionId.empty() && rule.sessionId == query.sessionId;
        case AiPermissionRuleDuration::profile:
            return !query.profileId.empty() && rule.profileId == query.profileId;
        case AiPermissionRuleDuration::global:
            return true;
    }
    return false;
}

[[nodiscard]] int dispositionPriority(const AiPermissionDisposition disposition) noexcept
{
    switch (disposition)
    {
        case AiPermissionDisposition::deny:
            return 0;
        case AiPermissionDisposition::ask:
            return 1;
        case AiPermissionDisposition::allow:
            return 2;
    }
    return 3;
}

} // namespace

bool AiPermissionRuleEngine::replace(std::vector<AiPermissionRule> rules)
{
    if (rules.size() > maximumRules)
    {
        return false;
    }
    std::unordered_set<std::string> ids;
    for (const auto &rule : rules)
    {
        if (!valid(rule) || !ids.insert(rule.id).second)
        {
            return false;
        }
    }
    m_rules = std::move(rules);
    return true;
}

bool AiPermissionRuleEngine::add(AiPermissionRule rule)
{
    if (m_rules.size() >= maximumRules || !valid(rule)
        || std::ranges::any_of(m_rules, [&rule](const AiPermissionRule &existing) {
               return existing.id == rule.id;
           }))
    {
        return false;
    }
    m_rules.push_back(std::move(rule));
    return true;
}

bool AiPermissionRuleEngine::remove(const std::string_view ruleId)
{
    const auto iterator = std::ranges::find(m_rules, ruleId, &AiPermissionRule::id);
    if (iterator == m_rules.end())
    {
        return false;
    }
    m_rules.erase(iterator);
    return true;
}

std::optional<AiPermissionRuleMatch> AiPermissionRuleEngine::evaluate(const AiPermissionRuleQuery &query)
{
    std::optional<std::size_t> bestIndex;
    int bestPriority = 4;
    for (std::size_t index = 0; index < m_rules.size(); ++index)
    {
        const auto &rule = m_rules[index];
        const int priority = dispositionPriority(rule.disposition);
        if (!rule.enabled || priority >= bestPriority || rule.capability != query.capability
            || !scopeMatches(rule, query) || !subjectMatches(rule, query.subject))
        {
            continue;
        }
        bestIndex = index;
        bestPriority = priority;
    }
    if (!bestIndex.has_value())
    {
        return std::nullopt;
    }
    const AiPermissionRule rule = m_rules[*bestIndex];
    if (rule.duration == AiPermissionRuleDuration::once)
    {
        m_rules.erase(m_rules.begin() + static_cast<std::ptrdiff_t>(*bestIndex));
    }
    return AiPermissionRuleMatch{.ruleId = rule.id, .disposition = rule.disposition, .duration = rule.duration};
}

void AiPermissionRuleEngine::clearSession(const std::string_view sessionId)
{
    std::erase_if(m_rules, [sessionId](const AiPermissionRule &rule) {
        return (rule.duration == AiPermissionRuleDuration::once || rule.duration == AiPermissionRuleDuration::session)
               && rule.sessionId == sessionId;
    });
}

const std::vector<AiPermissionRule> &AiPermissionRuleEngine::rules() const noexcept
{
    return m_rules;
}

bool AiPermissionRuleEngine::valid(const AiPermissionRule &rule)
{
    constexpr std::size_t maximumIdentityBytes = 128;
    constexpr std::size_t maximumPatternBytes = 1024;
    if (rule.id.empty() || rule.id.size() > maximumIdentityBytes || rule.pattern.size() > maximumPatternBytes)
    {
        return false;
    }
    if (rule.matcher != AiPermissionRuleMatcher::all && rule.pattern.empty())
    {
        return false;
    }
    if ((rule.duration == AiPermissionRuleDuration::once || rule.duration == AiPermissionRuleDuration::session)
        && (rule.sessionId.empty() || rule.sessionId.size() > maximumIdentityBytes))
    {
        return false;
    }
    if (rule.duration == AiPermissionRuleDuration::profile
        && (rule.profileId.empty() || rule.profileId.size() > maximumIdentityBytes))
    {
        return false;
    }
    if (rule.matcher == AiPermissionRuleMatcher::regex)
    {
        try
        {
            static_cast<void>(std::regex(rule.pattern, std::regex_constants::ECMAScript));
        }
        catch (const std::regex_error &)
        {
            return false;
        }
    }
    return true;
}

AiPermissionDecision AiPermissionPolicy::decide(const AiPermissionRequest &request) const noexcept
{
    if (!request.schemaValid)
    {
        return {.reason = AiPermissionReason::invalidSchema};
    }
    if (!request.scopeValid)
    {
        return {.reason = AiPermissionReason::invalidScope};
    }
    if (!request.capabilityAvailable)
    {
        return {.reason = AiPermissionReason::unavailableCapability};
    }
    if (request.explicitDeny)
    {
        return {.reason = AiPermissionReason::explicitDeny};
    }
    if (!request.write)
    {
        return {.disposition = AiPermissionDisposition::allow, .reason = AiPermissionReason::explicitAllow};
    }
    if (request.explicitVisibleApproval)
    {
        return {.disposition = AiPermissionDisposition::allow, .reason = AiPermissionReason::explicitVisibleApproval};
    }
    if (request.explicitAsk)
    {
        return {.disposition = AiPermissionDisposition::ask, .reason = AiPermissionReason::explicitAsk};
    }

    AiPermissionDecision decision;
    if (request.explicitAllow)
    {
        decision = {.disposition = AiPermissionDisposition::allow, .reason = AiPermissionReason::explicitAllow};
    }
    else
    {
        switch (request.mode)
        {
            case AiPermissionMode::readOnly:
                decision = {.reason = AiPermissionReason::readOnlyMode};
                break;
            case AiPermissionMode::ask:
                decision = {.disposition = AiPermissionDisposition::ask, .reason = AiPermissionReason::askMode};
                break;
            case AiPermissionMode::edit:
                decision = {.disposition = AiPermissionDisposition::ask, .reason = AiPermissionReason::editMode};
                break;
            case AiPermissionMode::automatic:
                decision = {.disposition = AiPermissionDisposition::allow, .reason = AiPermissionReason::automaticMode};
                break;
            case AiPermissionMode::yolo:
                decision = {.disposition = AiPermissionDisposition::allow, .reason = AiPermissionReason::yoloMode};
                break;
        }
    }
    return decision;
}

AiCommandRiskReport AiPermissionPolicy::classifyCommand(const std::string_view command)
{
    if (command.contains('\0'))
    {
        return risk(AiCommandRiskCategory::opaqueDownloadAndExecute,
                    "The command contains an embedded NUL byte and cannot be inspected safely.");
    }
    const std::string normalized = normalizedCommand(command);
    if (containsAny(normalized,
                    std::array{std::string_view{"rm -rf"}, std::string_view{"rm -fr"},
                               std::string_view{"remove-item -recurse -force"},
                               std::string_view{"remove-item -force -recurse"}, std::string_view{"rmdir /s"},
                               std::string_view{"rd /s"}, std::string_view{"del /s"}}))
    {
        return risk(AiCommandRiskCategory::destructiveFilesystem,
                    "The command performs a recursive or forced filesystem deletion.");
    }
    if (containsAny(normalized,
                    std::array{std::string_view{"format "}, std::string_view{"format.com "}, std::string_view{"mkfs"},
                               std::string_view{"diskpart"}, std::string_view{"clear-disk"},
                               std::string_view{"initialize-disk"}, std::string_view{"dd if="}}))
    {
        return risk(AiCommandRiskCategory::destructiveDisk,
                    "The command can format, initialize, or overwrite a disk device.");
    }
    if (containsAny(normalized, std::array{std::string_view{"chmod -r"}, std::string_view{"chown -r"},
                                           std::string_view{"icacls "}, std::string_view{"takeown /r"}}))
    {
        return risk(AiCommandRiskCategory::recursivePermission,
                    "The command changes permissions or ownership recursively.");
    }
    if (containsAny(normalized,
                    std::array{std::string_view{"passwd"}, std::string_view{"chpasswd"}, std::string_view{"visudo"},
                               std::string_view{"useradd"}, std::string_view{"userdel"}, std::string_view{"net user"},
                               std::string_view{"set-localuser"}, std::string_view{"new-localuser"}}))
    {
        return risk(AiCommandRiskCategory::privilegeOrCredential,
                    "The command modifies accounts, credentials, or privilege policy.");
    }
    if (containsAny(normalized, std::array{std::string_view{"shutdown"}, std::string_view{"restart-computer"},
                                           std::string_view{"stop-computer"}, std::string_view{"reboot"},
                                           std::string_view{"systemctl poweroff"}}))
    {
        return risk(AiCommandRiskCategory::shutdownOrReboot, "The command can shut down or restart a machine.");
    }
    if (containsAny(normalized, std::array{std::string_view{"iptables -f"}, std::string_view{"nft flush"},
                                           std::string_view{"ufw disable"}, std::string_view{"netsh advfirewall"},
                                           std::string_view{"disable-netadapter"}, std::string_view{"ip link set"}}))
    {
        return risk(AiCommandRiskCategory::networkDisruption,
                    "The command can disable networking or replace firewall policy.");
    }

    const bool downloads =
        containsAny(normalized, std::array{std::string_view{"curl "}, std::string_view{"wget "},
                                           std::string_view{"invoke-webrequest"}, std::string_view{"iwr "}});
    const bool pipesOrEvaluates = containsAny(
        normalized, std::array{std::string_view{"| sh"}, std::string_view{"| bash"}, std::string_view{"| zsh"},
                               std::string_view{"| pwsh"}, std::string_view{"| powershell"},
                               std::string_view{"invoke-expression"}, std::string_view{"iex "}});
    if ((downloads && pipesOrEvaluates)
        || containsAny(normalized,
                       std::array{std::string_view{"powershell -enc"}, std::string_view{"powershell.exe -enc"},
                                  std::string_view{"pwsh -enc"}, std::string_view{"frombase64string("}}))
    {
        return risk(AiCommandRiskCategory::opaqueDownloadAndExecute,
                    "The command downloads and executes content or uses an opaque encoded payload.");
    }
    return {};
}

} // namespace ztermy::ai
