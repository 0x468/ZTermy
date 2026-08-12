#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

enum class AiPermissionCapability : std::uint8_t
{
    terminalCommand,
    ptyInput,
    terminalInterrupt,
    runbookMutation,
    sftpDownload,
    sftpUpload,
    mcpTool,
};

enum class AiPermissionRuleMatcher : std::uint8_t
{
    exact,
    prefix,
    glob,
    regex,
    all,
};

enum class AiPermissionRuleDuration : std::uint8_t
{
    once,
    session,
    profile,
    global,
};

struct AiPermissionRule final
{
    std::string id;
    AiPermissionCapability capability = AiPermissionCapability::terminalCommand;
    AiPermissionRuleMatcher matcher = AiPermissionRuleMatcher::exact;
    std::string pattern;
    AiPermissionDisposition disposition = AiPermissionDisposition::ask;
    AiPermissionRuleDuration duration = AiPermissionRuleDuration::session;
    std::string sessionId;
    std::string profileId;
    bool enabled = true;

    bool operator==(const AiPermissionRule &) const = default;
};

struct AiPermissionRuleQuery final
{
    AiPermissionCapability capability = AiPermissionCapability::terminalCommand;
    std::string_view subject;
    std::string_view sessionId;
    std::string_view profileId;
};

struct AiPermissionRuleMatch final
{
    std::string ruleId;
    AiPermissionDisposition disposition = AiPermissionDisposition::ask;
    AiPermissionRuleDuration duration = AiPermissionRuleDuration::session;
};

class AiPermissionRuleEngine final
{
public:
    static constexpr std::size_t maximumRules = 256;

    [[nodiscard]] bool replace(std::vector<AiPermissionRule> rules);
    [[nodiscard]] bool add(AiPermissionRule rule);
    [[nodiscard]] bool remove(std::string_view ruleId);
    [[nodiscard]] std::optional<AiPermissionRuleMatch> evaluate(const AiPermissionRuleQuery &query);
    void clearSession(std::string_view sessionId);
    [[nodiscard]] const std::vector<AiPermissionRule> &rules() const noexcept;
    [[nodiscard]] static bool valid(const AiPermissionRule &rule);

private:
    std::vector<AiPermissionRule> m_rules;
};

enum class AiPermissionReason : std::uint8_t
{
    invalidSchema,
    invalidScope,
    unavailableCapability,
    explicitDeny,
    explicitAsk,
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
    bool explicitAsk = false;
    bool explicitVisibleApproval = false;
    bool explicitAllow = false;
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
