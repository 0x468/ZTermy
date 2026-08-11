#include "application/ai/AiActionToolDispatcher.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include <cmath>
#include <limits>
#include <utility>

namespace ztermy::ai
{
namespace
{

constexpr std::size_t maximumCommandBytes = std::size_t{16} * 1024;
constexpr std::size_t maximumCommandIdBytes = 256;
constexpr std::size_t maximumArgumentsBytes = std::size_t{20} * 1024;

[[nodiscard]] QString text(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::string json(const QJsonObject &value)
{
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] std::string failure(const QString &code, const QString &message)
{
    return json(QJsonObject{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}});
}

[[nodiscard]] bool hasOnlyKeys(const QJsonObject &value, const QSet<QString> &allowed)
{
    for (auto entry = value.constBegin(); entry != value.constEnd(); ++entry)
    {
        if (!allowed.contains(entry.key()))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::uint64_t> generation(const QJsonValue &value)
{
    if (!value.isDouble())
    {
        return std::nullopt;
    }
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number
        || number > static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
    {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(number);
}

[[nodiscard]] QString budgetCode(const AiAgentBudgetDecision decision)
{
    switch (decision)
    {
        case AiAgentBudgetDecision::allow:
            return QStringLiteral("ok");
        case AiAgentBudgetDecision::toolCallLimit:
            return QStringLiteral("tool_call_limit");
        case AiAgentBudgetDecision::writeActionLimit:
            return QStringLiteral("write_action_limit");
        case AiAgentBudgetDecision::repeatedReadLimit:
            return QStringLiteral("repeated_read_limit");
        case AiAgentBudgetDecision::tokenLimit:
            return QStringLiteral("token_limit");
        case AiAgentBudgetDecision::timeLimit:
            return QStringLiteral("time_limit");
    }
    return QStringLiteral("tool_call_limit");
}

[[nodiscard]] AiActionToolPlan response(std::string output, const bool sideEffecting = true)
{
    return AiActionToolPlan{.outputJson = std::move(output), .sideEffecting = sideEffecting};
}

} // namespace

std::vector<AiToolDefinition> AiActionToolDispatcher::definitions()
{
    return {
        {.name = "run_command",
         .description = "Queue one command in the exact target terminal session. Returns after input is accepted, not "
                        "after the command finishes.",
         .parametersJson =
             R"({"type":"object","properties":{"session_id":{"type":"string"},"session_generation":{"type":"integer","minimum":0},"command":{"type":"string","minLength":1,"maxLength":16384}},"required":["session_id","session_generation","command"],"additionalProperties":false})"},
        {.name = "interrupt_command",
         .description = "Request a soft interrupt for a tracked command in the exact target terminal session by "
                        "writing Ctrl+C. The command outcome remains unknown until observed.",
         .parametersJson =
             R"({"type":"object","properties":{"command_id":{"type":"string","minLength":1,"maxLength":256},"session_id":{"type":"string","minLength":1},"session_generation":{"type":"integer","minimum":0},"mode":{"type":"string","enum":["soft"]}},"required":["command_id","session_id","session_generation","mode"],"additionalProperties":false})"}};
}

AiActionToolPlan AiActionToolDispatcher::prepare(const AiToolCall &call, const AiActionToolContext &context,
                                                 AiAgentTurnBudget &budget)
{
    const bool runCommand = call.name == "run_command";
    const bool interruptCommand = call.name == "interrupt_command";
    if (!runCommand && !interruptCommand)
    {
        return response(
            failure(QStringLiteral("unsupported"), QStringLiteral("The requested action tool is unsupported.")), false);
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(QByteArray::fromStdString(call.argumentsJson), &parseError);
    const auto object = document.object();
    const auto requestedGeneration = generation(object.value(QStringLiteral("session_generation")));
    const auto requestedSession = object.value(QStringLiteral("session_id"));
    const auto requestedCommand = object.value(QStringLiteral("command"));
    const auto requestedCommandId = object.value(QStringLiteral("command_id"));
    const auto requestedMode = object.value(QStringLiteral("mode"));
    const bool commonSchemaValid = call.argumentsJson.size() <= maximumArgumentsBytes && document.isObject()
                                   && parseError.error == QJsonParseError::NoError && requestedSession.isString()
                                   && !requestedSession.toString().isEmpty() && requestedGeneration.has_value();
    const bool runSchemaValid =
        runCommand && commonSchemaValid
        && hasOnlyKeys(object,
                       {QStringLiteral("session_id"), QStringLiteral("session_generation"), QStringLiteral("command")})
        && requestedCommand.isString() && !requestedCommand.toString().trimmed().isEmpty()
        && std::cmp_less_equal(requestedCommand.toString().toUtf8().size(), maximumCommandBytes)
        && !requestedCommand.toString().contains(QChar::Null);
    const bool interruptSchemaValid =
        interruptCommand && commonSchemaValid
        && hasOnlyKeys(object, {QStringLiteral("command_id"), QStringLiteral("session_id"),
                                QStringLiteral("session_generation"), QStringLiteral("mode")})
        && requestedCommandId.isString() && !requestedCommandId.toString().isEmpty()
        && std::cmp_less_equal(requestedCommandId.toString().toUtf8().size(), maximumCommandIdBytes)
        && !requestedCommandId.toString().contains(QChar::Null) && requestedMode.isString()
        && requestedMode.toString() == QStringLiteral("soft");
    const bool schemaValid = runSchemaValid || interruptSchemaValid;
    const auto sessionId = requestedSession.toString().toUtf8().toStdString();
    const auto command = requestedCommand.toString().toUtf8().toStdString();
    const auto commandId = requestedCommandId.toString().toUtf8().toStdString();
    const bool scopeValid = schemaValid && sessionId == context.target.sessionId
                            && *requestedGeneration == context.target.sessionGeneration;

    QJsonObject canonical{
        {QStringLiteral("session_generation"),
         requestedGeneration.has_value() ? QJsonValue{static_cast<qint64>(*requestedGeneration)} : QJsonValue::Null},
        {QStringLiteral("session_id"), requestedSession}};
    if (runCommand)
    {
        canonical.insert(QStringLiteral("command"), requestedCommand);
    }
    else
    {
        canonical.insert(QStringLiteral("command_id"), requestedCommandId);
        canonical.insert(QStringLiteral("mode"), requestedMode);
    }
    AiToolDispatchRequest request{
        .key = {.conversationId = context.conversationId, .turnId = context.turnId, .toolCallId = call.id},
        .toolName = call.name,
        .canonicalArguments = json(canonical),
        .sessionId = context.target.sessionId,
        .sessionGeneration = context.target.sessionGeneration,
        .sideEffecting = true};
    const auto admitted = m_ledger.begin(std::move(request));
    if (admitted.admission == AiToolDispatchAdmission::cached && admitted.record.has_value())
    {
        return response(admitted.record->resultJson);
    }
    if (admitted.admission == AiToolDispatchAdmission::duplicateMismatch)
    {
        return response(failure(QStringLiteral("duplicate_mismatch"),
                                QStringLiteral("The tool-call id was reused with different arguments.")));
    }
    if (admitted.admission != AiToolDispatchAdmission::accepted)
    {
        return response(failure(QStringLiteral("tool_in_progress"),
                                QStringLiteral("An identical tool call is already being handled.")));
    }

    const auto key = admitted.record.value_or(AiToolDispatchRecord{}).request.key;
    const auto fail = [this, &key](const QString &code, const QString &message) {
        auto output = failure(code, message);
        static_cast<void>(m_ledger.transition(key, AiToolDispatchState::failed, output));
        return response(std::move(output));
    };
    if (!schemaValid)
    {
        return fail(QStringLiteral("invalid_arguments"), QStringLiteral("The terminal-action arguments are invalid."));
    }
    if (!scopeValid)
    {
        return fail(QStringLiteral("scope_changed"), QStringLiteral("The target terminal session changed."));
    }
    if (!context.writable)
    {
        return fail(QStringLiteral("session_unavailable"),
                    QStringLiteral("The target terminal session is not writable."));
    }
    const auto risk = runCommand ? AiPermissionPolicy::classifyCommand(command) : AiCommandRiskReport{};
    const auto decision =
        m_permissionPolicy.decide(AiPermissionRequest{.mode = context.permissionMode,
                                                      .write = true,
                                                      .schemaValid = schemaValid,
                                                      .scopeValid = scopeValid,
                                                      .firstWriteApproved = context.firstWriteApproved,
                                                      .savedHost = context.savedHost,
                                                      .highRisk = risk.highRisk(),
                                                      .highRiskSessionGrant = context.highRiskSessionGrant});
    AiTerminalAction action{.dispatchKey = key,
                            .target = context.target,
                            .kind =
                                runCommand ? AiTerminalActionKind::runCommand : AiTerminalActionKind::interruptCommand,
                            .command = command,
                            .commandId = commandId,
                            .risk = risk};
    if (decision.disposition == AiPermissionDisposition::deny)
    {
        return fail(QStringLiteral("permission_denied"),
                    QStringLiteral("The active permission mode denied the command."));
    }
    const auto budgetDecision = budget.authorize(true);
    if (budgetDecision != AiAgentBudgetDecision::allow)
    {
        return fail(budgetCode(budgetDecision), QStringLiteral("The AI turn action budget was exhausted."));
    }
    const auto ownership = m_ownership.claim(context.target, context.conversationId);
    if (ownership != AiWriteOwnershipResult::acquired && ownership != AiWriteOwnershipResult::alreadyOwned)
    {
        return fail(QStringLiteral("ownership_conflict"),
                    QStringLiteral("Another AI conversation owns terminal write control."));
    }
    if (decision.disposition == AiPermissionDisposition::ask)
    {
        static_cast<void>(m_ledger.transition(key, AiToolDispatchState::awaitingApproval));
        return AiActionToolPlan{.disposition = AiActionToolDisposition::awaitApproval,
                                .action = std::move(action),
                                .sideEffecting = true};
    }
    static_cast<void>(m_ledger.transition(key, AiToolDispatchState::running));
    return AiActionToolPlan{.disposition = AiActionToolDisposition::execute,
                            .action = std::move(action),
                            .sideEffecting = true};
}

bool AiActionToolDispatcher::approve(const AiTerminalAction &action)
{
    return m_ledger.transition(action.dispatchKey, AiToolDispatchState::running);
}

bool AiActionToolDispatcher::deny(const AiTerminalAction &action)
{
    return m_ledger.transition(
        action.dispatchKey, AiToolDispatchState::failed,
        failure(QStringLiteral("permission_denied"), QStringLiteral("The user denied this command.")));
}

bool AiActionToolDispatcher::complete(const AiTerminalAction &action, const AiToolDispatchState state,
                                      std::string resultJson)
{
    return m_ledger.transition(action.dispatchKey, state, std::move(resultJson));
}

void AiActionToolDispatcher::clearConversation(const std::string_view conversationId)
{
    m_ownership.releaseConversation(conversationId);
    m_ledger.clearConversation(conversationId);
}

} // namespace ztermy::ai
