#include "domain/ai/AiPermissionPolicy.h"

#include <algorithm>
#include <array>
#include <cctype>
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

} // namespace

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

    AiPermissionDecision decision;
    if (request.explicitAllow)
    {
        decision = {.disposition = AiPermissionDisposition::allow, .reason = AiPermissionReason::explicitAllow};
    }
    else
    {
        switch (request.mode)
        {
            case AiPermissionMode::observer:
                decision = {.reason = AiPermissionReason::observerMode};
                break;
            case AiPermissionMode::askEachWrite:
                decision = {.disposition = AiPermissionDisposition::ask,
                            .reason = AiPermissionReason::askEachWriteMode};
                break;
            case AiPermissionMode::askFirstWrite:
                decision = request.firstWriteApproved
                               ? AiPermissionDecision{.disposition = AiPermissionDisposition::allow,
                                                      .reason = AiPermissionReason::askFirstWriteGrant}
                               : AiPermissionDecision{.disposition = AiPermissionDisposition::ask,
                                                      .reason = AiPermissionReason::askFirstWriteMode};
                break;
            case AiPermissionMode::sessionAuto:
                decision = {.disposition = AiPermissionDisposition::allow,
                            .reason = AiPermissionReason::sessionAutoMode};
                break;
            case AiPermissionMode::savedHostAuto:
                decision = request.savedHost ? AiPermissionDecision{.disposition = AiPermissionDisposition::allow,
                                                                    .reason = AiPermissionReason::savedHostAutoMode}
                                             : AiPermissionDecision{.disposition = AiPermissionDisposition::ask,
                                                                    .reason = AiPermissionReason::savedHostRequired};
                break;
        }
    }

    if (decision.disposition == AiPermissionDisposition::allow && request.highRisk && !request.highRiskSessionGrant)
    {
        return {.disposition = AiPermissionDisposition::ask, .reason = AiPermissionReason::highRiskOverlay};
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
