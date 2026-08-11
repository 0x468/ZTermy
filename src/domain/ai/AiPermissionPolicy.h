#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ztermy::ai
{

enum class AiPermissionMode : std::uint8_t
{
    observer,
    askEachWrite,
    askFirstWrite,
    sessionAuto,
    savedHostAuto,
};

enum class AiPermissionDisposition : std::uint8_t
{
    allow,
    ask,
    deny,
};

enum class AiPermissionReason : std::uint8_t
{
    invalidSchema,
    invalidScope,
    unavailableCapability,
    explicitDeny,
    explicitVisibleApproval,
    explicitAllow,
    observerMode,
    askEachWriteMode,
    askFirstWriteMode,
    askFirstWriteGrant,
    sessionAutoMode,
    savedHostAutoMode,
    savedHostRequired,
    highRiskOverlay,
    defaultDeny,
};

enum class AiCommandRiskCategory : std::uint8_t
{
    none,
    destructiveFilesystem,
    destructiveDisk,
    recursivePermission,
    privilegeOrCredential,
    shutdownOrReboot,
    networkDisruption,
    opaqueDownloadAndExecute,
};

struct AiCommandRiskReport final
{
    AiCommandRiskCategory category = AiCommandRiskCategory::none;
    std::string reason;

    [[nodiscard]] bool highRisk() const noexcept { return category != AiCommandRiskCategory::none; }
};

struct AiPermissionRequest final
{
    AiPermissionMode mode = AiPermissionMode::observer;
    bool write = false;
    bool schemaValid = true;
    bool scopeValid = true;
    bool capabilityAvailable = true;
    bool explicitDeny = false;
    bool explicitVisibleApproval = false;
    bool explicitAllow = false;
    bool firstWriteApproved = false;
    bool savedHost = false;
    bool highRisk = false;
    bool highRiskSessionGrant = false;
};

struct AiPermissionDecision final
{
    AiPermissionDisposition disposition = AiPermissionDisposition::deny;
    AiPermissionReason reason = AiPermissionReason::defaultDeny;
};

class AiPermissionPolicy final
{
public:
    [[nodiscard]] AiPermissionDecision decide(const AiPermissionRequest &request) const noexcept;
    [[nodiscard]] static AiCommandRiskReport classifyCommand(std::string_view command);
};

} // namespace ztermy::ai
