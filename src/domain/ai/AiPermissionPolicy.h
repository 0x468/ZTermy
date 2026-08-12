#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ztermy::ai
{

enum class AiPermissionMode : std::uint8_t
{
    readOnly,
    ask,
    edit,
    automatic,
    yolo,
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
    readOnlyMode,
    askMode,
    editMode,
    automaticMode,
    yoloMode,
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
    AiPermissionMode mode = AiPermissionMode::readOnly;
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
