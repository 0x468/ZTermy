#pragma once

#include "domain/ai/AiAgentGuard.h"
#include "domain/ai/AiPermissionPolicy.h"
#include "domain/ai/AiProviderTypes.h"
#include "domain/ai/AiToolDispatchLedger.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ztermy::ai
{

struct AiActionToolContext final
{
    std::string conversationId;
    std::uint64_t turnId = 0;
    AiSessionTarget target;
    AiPermissionMode permissionMode = AiPermissionMode::ask;
    std::string profileId;
    bool writable = true;
};

enum class AiActionToolDisposition : std::uint8_t
{
    execute,
    awaitApproval,
    respond,
};

enum class AiTerminalActionKind : std::uint8_t
{
    runCommand,
    writeToPty,
    interruptCommand,
    saveRunbook,
    enqueueSftpDownload,
    enqueueSftpUpload,
};

struct AiTerminalAction final
{
    AiToolDispatchKey dispatchKey;
    AiSessionTarget target;
    AiTerminalActionKind kind = AiTerminalActionKind::runCommand;
    std::string command;
    std::string commandId;
    std::string ptyData;
    std::string payloadJson;
    std::string permissionSubject;
    AiPermissionCapability permissionCapability = AiPermissionCapability::terminalCommand;
    bool appendEnter = false;
    AiCommandRiskReport risk;
};

struct AiActionToolPlan final
{
    AiActionToolDisposition disposition = AiActionToolDisposition::respond;
    std::optional<AiTerminalAction> action;
    std::string outputJson;
    bool sideEffecting = false;
};

class AiActionToolDispatcher final
{
public:
    [[nodiscard]] static std::vector<AiToolDefinition> definitions();
    [[nodiscard]] AiActionToolPlan prepare(const AiToolCall &call, const AiActionToolContext &context,
                                           AiAgentTurnBudget &budget);
    [[nodiscard]] AiPermissionDecision permissionDecision(AiPermissionCapability capability, std::string_view subject,
                                                          const AiActionToolContext &context, bool highRisk = false);
    [[nodiscard]] bool approve(const AiTerminalAction &action);
    [[nodiscard]] bool deny(const AiTerminalAction &action);
    [[nodiscard]] bool complete(const AiTerminalAction &action, AiToolDispatchState state, std::string resultJson);
    [[nodiscard]] bool agentHasControl(const AiSessionTarget &target, std::string_view conversationId) const;
    [[nodiscard]] bool handoffToUser(const AiSessionTarget &target, std::string_view conversationId);
    [[nodiscard]] bool resumeAgent(const AiSessionTarget &target, std::string_view conversationId);
    [[nodiscard]] bool replacePermissionRules(std::vector<AiPermissionRule> rules);
    [[nodiscard]] bool addPermissionRule(AiPermissionRule rule);
    [[nodiscard]] bool removePermissionRule(std::string_view ruleId);
    [[nodiscard]] const std::vector<AiPermissionRule> &permissionRules() const noexcept;
    void clearSession(const AiSessionTarget &target);
    void clearConversation(std::string_view conversationId);

private:
    AiPermissionPolicy m_permissionPolicy;
    AiPermissionRuleEngine m_permissionRules;
    AiToolDispatchLedger m_ledger{AiToolDispatchLimits{.maximumArgumentsBytes = std::size_t{20} * 1024}};
    AiSessionWriteOwnership m_ownership;
};

} // namespace ztermy::ai
