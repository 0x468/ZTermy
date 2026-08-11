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
    AiPermissionMode permissionMode = AiPermissionMode::askEachWrite;
    bool savedHost = false;
    bool firstWriteApproved = false;
    bool highRiskSessionGrant = false;
};

enum class AiActionToolDisposition : std::uint8_t
{
    execute,
    awaitApproval,
    respond,
};

struct AiRunCommandAction final
{
    AiToolDispatchKey dispatchKey;
    AiSessionTarget target;
    std::string command;
    AiCommandRiskReport risk;
};

struct AiActionToolPlan final
{
    AiActionToolDisposition disposition = AiActionToolDisposition::respond;
    std::optional<AiRunCommandAction> runCommand;
    std::string outputJson;
    bool sideEffecting = false;
};

class AiActionToolDispatcher final
{
public:
    [[nodiscard]] static std::vector<AiToolDefinition> definitions();
    [[nodiscard]] AiActionToolPlan prepare(const AiToolCall &call, const AiActionToolContext &context,
                                           AiAgentTurnBudget &budget);
    [[nodiscard]] bool approve(const AiRunCommandAction &action);
    [[nodiscard]] bool deny(const AiRunCommandAction &action);
    [[nodiscard]] bool complete(const AiRunCommandAction &action, AiToolDispatchState state, std::string resultJson);
    void clearConversation(std::string_view conversationId);

private:
    AiPermissionPolicy m_permissionPolicy;
    AiToolDispatchLedger m_ledger;
    AiSessionWriteOwnership m_ownership;
};

} // namespace ztermy::ai
