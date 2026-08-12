#include "application/ai/AiActionToolDispatcher.h"

#include <QJsonArray>
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
constexpr std::size_t maximumPtyInputBytes = std::size_t{4} * 1024;
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

[[nodiscard]] bool validPtyInput(const QString &value)
{
    if (value.isEmpty() || std::cmp_greater(value.toUtf8().size(), maximumPtyInputBytes))
    {
        return false;
    }
    for (const QChar character : value)
    {
        const auto codePoint = character.unicode();
        if ((codePoint < 0x20U && codePoint != '\t') || codePoint == 0x7FU || codePoint == 0x85U || codePoint == 0x2028U
            || codePoint == 0x2029U)
        {
            return false;
        }
    }
    return true;
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

[[nodiscard]] AiPermissionDisposition mutationDisposition(const AiPermissionMode mode,
                                                          const std::optional<AiPermissionRuleMatch> &rule)
{
    if (rule.has_value())
    {
        return rule->disposition;
    }
    switch (mode)
    {
        case AiPermissionMode::readOnly:
            return AiPermissionDisposition::deny;
        case AiPermissionMode::ask:
            return AiPermissionDisposition::ask;
        case AiPermissionMode::edit:
        case AiPermissionMode::automatic:
        case AiPermissionMode::yolo:
            return AiPermissionDisposition::allow;
    }
    return AiPermissionDisposition::deny;
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
             R"({"type":"object","properties":{"command_id":{"type":"string","minLength":1,"maxLength":256},"session_id":{"type":"string","minLength":1},"session_generation":{"type":"integer","minimum":0},"mode":{"type":"string","enum":["soft"]}},"required":["command_id","session_id","session_generation","mode"],"additionalProperties":false})"},
        {.name = "write_to_pty",
         .description = "Write bounded UTF-8 text to the exact target PTY, optionally followed by Enter. The input "
                        "is not treated as a semantic command and is never echoed in the tool result.",
         .parametersJson =
             R"({"type":"object","properties":{"session_id":{"type":"string","minLength":1},"session_generation":{"type":"integer","minimum":0},"data":{"type":"string","minLength":1,"maxLength":4096},"append_enter":{"type":"boolean"}},"required":["session_id","session_generation","data","append_enter"],"additionalProperties":false})"},
        {.name = "save_runbook",
         .description = "Save a reusable ztermy script according to the active agent mode and rules.",
         .parametersJson =
             R"({"type":"object","properties":{"session_id":{"type":"string","minLength":1},"session_generation":{"type":"integer","minimum":0},"runbook":{"type":"object","properties":{"name":{"type":"string","minLength":1,"maxLength":128},"description":{"type":"string","maxLength":4096},"shell":{"type":"string","enum":["any","powershell","bash","zsh","fish","sh"]},"steps":{"type":"array","minItems":1,"maxItems":64,"items":{"type":"object","properties":{"command":{"type":"string","minLength":1,"maxLength":16384},"continuation":{"type":"string","enum":["immediate","literal-output"]},"output_marker":{"type":"string","maxLength":1024},"timeout_ms":{"type":"integer","minimum":0,"maximum":4294967295}},"required":["command","continuation","output_marker","timeout_ms"],"additionalProperties":false}}},"required":["name","description","shell","steps"],"additionalProperties":false}},"required":["session_id","session_generation","runbook"],"additionalProperties":false})"},
        {.name = "queue_sftp_download",
         .description = "Queue one remote regular file for download according to the active agent mode and rules.",
         .parametersJson =
             R"({"type":"object","properties":{"session_id":{"type":"string","minLength":1},"session_generation":{"type":"integer","minimum":0},"remote_path":{"type":"string","minLength":1,"maxLength":4096},"local_path":{"type":"string","minLength":1,"maxLength":4096}},"required":["session_id","session_generation","remote_path","local_path"],"additionalProperties":false})"},
        {.name = "queue_sftp_upload",
         .description =
             "Queue one local regular non-symlink file for upload according to the active agent mode and rules.",
         .parametersJson =
             R"({"type":"object","properties":{"session_id":{"type":"string","minLength":1},"session_generation":{"type":"integer","minimum":0},"local_path":{"type":"string","minLength":1,"maxLength":4096},"remote_path":{"type":"string","minLength":1,"maxLength":4096}},"required":["session_id","session_generation","local_path","remote_path"],"additionalProperties":false})"}};
}

AiActionToolPlan AiActionToolDispatcher::prepare(const AiToolCall &call, const AiActionToolContext &context,
                                                 AiAgentTurnBudget &budget)
{
    const bool runCommand = call.name == "run_command";
    const bool interruptCommand = call.name == "interrupt_command";
    const bool writeToPty = call.name == "write_to_pty";
    const bool saveRunbook = call.name == "save_runbook";
    const bool sftpDownload = call.name == "queue_sftp_download";
    const bool sftpUpload = call.name == "queue_sftp_upload";
    if (!runCommand && !interruptCommand && !writeToPty && !saveRunbook && !sftpDownload && !sftpUpload)
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
    const auto requestedPtyData = object.value(QStringLiteral("data"));
    const auto requestedAppendEnter = object.value(QStringLiteral("append_enter"));
    const auto requestedRunbook = object.value(QStringLiteral("runbook"));
    const auto requestedLocalPath = object.value(QStringLiteral("local_path"));
    const auto requestedRemotePath = object.value(QStringLiteral("remote_path"));
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
    const bool writeSchemaValid =
        writeToPty && commonSchemaValid
        && hasOnlyKeys(object, {QStringLiteral("session_id"), QStringLiteral("session_generation"),
                                QStringLiteral("data"), QStringLiteral("append_enter")})
        && requestedPtyData.isString() && validPtyInput(requestedPtyData.toString()) && requestedAppendEnter.isBool();
    bool runbookSchemaValid = saveRunbook && commonSchemaValid
                              && hasOnlyKeys(object, {QStringLiteral("session_id"),
                                                      QStringLiteral("session_generation"), QStringLiteral("runbook")})
                              && requestedRunbook.isObject();
    if (runbookSchemaValid)
    {
        const QJsonObject runbook = requestedRunbook.toObject();
        const QString shell = runbook.value(QStringLiteral("shell")).toString();
        const QJsonArray steps = runbook.value(QStringLiteral("steps")).toArray();
        const QSet<QString> shells{QStringLiteral("any"), QStringLiteral("powershell"), QStringLiteral("bash"),
                                   QStringLiteral("zsh"), QStringLiteral("fish"),       QStringLiteral("sh")};
        runbookSchemaValid = hasOnlyKeys(runbook, {QStringLiteral("name"), QStringLiteral("description"),
                                                   QStringLiteral("shell"), QStringLiteral("steps")})
                             && runbook.value(QStringLiteral("name")).isString()
                             && !runbook.value(QStringLiteral("name")).toString().trimmed().isEmpty()
                             && runbook.value(QStringLiteral("name")).toString().size() <= 128
                             && runbook.value(QStringLiteral("name")).toString().toUtf8().size() <= 512
                             && runbook.value(QStringLiteral("description")).isString()
                             && runbook.value(QStringLiteral("description")).toString().size() <= 4096
                             && runbook.value(QStringLiteral("description")).toString().toUtf8().size() <= 4096
                             && runbook.value(QStringLiteral("shell")).isString() && shells.contains(shell)
                             && runbook.value(QStringLiteral("steps")).isArray() && !steps.isEmpty()
                             && steps.size() <= 64;
        for (const auto &stepValue : steps)
        {
            const QJsonObject step = stepValue.toObject();
            const auto timeout = generation(step.value(QStringLiteral("timeout_ms")));
            const QString continuation = step.value(QStringLiteral("continuation")).toString();
            runbookSchemaValid =
                runbookSchemaValid && stepValue.isObject()
                && hasOnlyKeys(step, {QStringLiteral("command"), QStringLiteral("continuation"),
                                      QStringLiteral("output_marker"), QStringLiteral("timeout_ms")})
                && step.value(QStringLiteral("command")).isString()
                && !step.value(QStringLiteral("command")).toString().trimmed().isEmpty()
                && step.value(QStringLiteral("command")).toString().size() <= 16384
                && std::cmp_less_equal(step.value(QStringLiteral("command")).toString().toUtf8().size(),
                                       maximumCommandBytes)
                && !step.value(QStringLiteral("command")).toString().contains(QChar::Null)
                && (continuation == QStringLiteral("immediate") || continuation == QStringLiteral("literal-output"))
                && step.value(QStringLiteral("output_marker")).isString()
                && step.value(QStringLiteral("output_marker")).toString().size() <= 1024
                && step.value(QStringLiteral("output_marker")).toString().toUtf8().size() <= 1024 && timeout.has_value()
                && *timeout <= std::numeric_limits<std::uint32_t>::max();
        }
    }
    const bool sftpSchemaValid =
        (sftpDownload || sftpUpload) && commonSchemaValid
        && hasOnlyKeys(object, {QStringLiteral("session_id"), QStringLiteral("session_generation"),
                                QStringLiteral("local_path"), QStringLiteral("remote_path")})
        && requestedLocalPath.isString() && !requestedLocalPath.toString().trimmed().isEmpty()
        && requestedLocalPath.toString().size() <= 4096 && !requestedLocalPath.toString().contains(QChar::Null)
        && requestedRemotePath.isString() && !requestedRemotePath.toString().trimmed().isEmpty()
        && requestedRemotePath.toString().size() <= 4096 && !requestedRemotePath.toString().contains(QChar::Null);
    const bool schemaValid =
        runSchemaValid || interruptSchemaValid || writeSchemaValid || runbookSchemaValid || sftpSchemaValid;
    const auto sessionId = requestedSession.toString().toUtf8().toStdString();
    const auto command = requestedCommand.toString().toUtf8().toStdString();
    const auto commandId = requestedCommandId.toString().toUtf8().toStdString();
    const auto ptyData = requestedPtyData.toString().toUtf8().toStdString();
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
    else if (interruptCommand)
    {
        canonical.insert(QStringLiteral("command_id"), requestedCommandId);
        canonical.insert(QStringLiteral("mode"), requestedMode);
    }
    else if (writeToPty)
    {
        canonical.insert(QStringLiteral("data"), requestedPtyData);
        canonical.insert(QStringLiteral("append_enter"), requestedAppendEnter);
    }
    else if (saveRunbook)
    {
        canonical.insert(QStringLiteral("runbook"), requestedRunbook);
    }
    else
    {
        canonical.insert(QStringLiteral("local_path"), requestedLocalPath);
        canonical.insert(QStringLiteral("remote_path"), requestedRemotePath);
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
    if (saveRunbook)
    {
        const auto runbookName =
            requestedRunbook.toObject().value(QStringLiteral("name")).toString().toUtf8().toStdString();
        const auto rule = m_permissionRules.evaluate({.capability = AiPermissionCapability::runbookMutation,
                                                      .subject = runbookName,
                                                      .sessionId = context.target.sessionId,
                                                      .profileId = context.profileId});
        const auto disposition = mutationDisposition(context.permissionMode, rule);
        if (disposition == AiPermissionDisposition::deny)
        {
            return fail(QStringLiteral("permission_denied"),
                        QStringLiteral("The active mode or permission rule denied saving the runbook."));
        }
        const auto budgetDecision = budget.authorize(true);
        if (budgetDecision != AiAgentBudgetDecision::allow)
        {
            return fail(budgetCode(budgetDecision), QStringLiteral("The AI turn action budget was exhausted."));
        }
        const bool execute = disposition == AiPermissionDisposition::allow;
        static_cast<void>(
            m_ledger.transition(key, execute ? AiToolDispatchState::running : AiToolDispatchState::awaitingApproval));
        return AiActionToolPlan{
            .disposition = execute ? AiActionToolDisposition::execute : AiActionToolDisposition::awaitApproval,
            .action = AiTerminalAction{.dispatchKey = key,
                                       .target = context.target,
                                       .kind = AiTerminalActionKind::saveRunbook,
                                       .payloadJson = json(requestedRunbook.toObject()),
                                       .permissionSubject = runbookName,
                                       .permissionCapability = AiPermissionCapability::runbookMutation},
            .sideEffecting = true};
    }
    if (sftpDownload || sftpUpload)
    {
        const auto capability =
            sftpDownload ? AiPermissionCapability::sftpDownload : AiPermissionCapability::sftpUpload;
        const auto subject = requestedRemotePath.toString().toUtf8().toStdString();
        const auto rule = m_permissionRules.evaluate({.capability = capability,
                                                      .subject = subject,
                                                      .sessionId = context.target.sessionId,
                                                      .profileId = context.profileId});
        const auto disposition = mutationDisposition(context.permissionMode, rule);
        if (disposition == AiPermissionDisposition::deny)
        {
            return fail(QStringLiteral("permission_denied"),
                        QStringLiteral("The active mode or permission rule denied the SFTP mutation."));
        }
        const auto budgetDecision = budget.authorize(true);
        if (budgetDecision != AiAgentBudgetDecision::allow)
        {
            return fail(budgetCode(budgetDecision), QStringLiteral("The AI turn action budget was exhausted."));
        }
        const QJsonObject payload{{QStringLiteral("local_path"), requestedLocalPath},
                                  {QStringLiteral("remote_path"), requestedRemotePath}};
        const bool execute = disposition == AiPermissionDisposition::allow;
        static_cast<void>(
            m_ledger.transition(key, execute ? AiToolDispatchState::running : AiToolDispatchState::awaitingApproval));
        return AiActionToolPlan{
            .disposition = execute ? AiActionToolDisposition::execute : AiActionToolDisposition::awaitApproval,
            .action = AiTerminalAction{.dispatchKey = key,
                                       .target = context.target,
                                       .kind = sftpDownload ? AiTerminalActionKind::enqueueSftpDownload
                                                            : AiTerminalActionKind::enqueueSftpUpload,
                                       .payloadJson = json(payload),
                                       .permissionSubject = subject,
                                       .permissionCapability = capability},
            .sideEffecting = true};
    }
    if (!context.writable)
    {
        return fail(QStringLiteral("session_unavailable"),
                    QStringLiteral("The target terminal session is not writable."));
    }
    const auto risk = runCommand ? AiPermissionPolicy::classifyCommand(command) : AiCommandRiskReport{};
    const auto capability = runCommand   ? AiPermissionCapability::terminalCommand
                            : writeToPty ? AiPermissionCapability::ptyInput
                                         : AiPermissionCapability::terminalInterrupt;
    const std::string_view permissionSubject = runCommand   ? std::string_view{command}
                                               : writeToPty ? std::string_view{ptyData}
                                                            : std::string_view{commandId};
    const auto rule = m_permissionRules.evaluate({.capability = capability,
                                                  .subject = permissionSubject,
                                                  .sessionId = context.target.sessionId,
                                                  .profileId = context.profileId});
    const auto decision = m_permissionPolicy.decide(
        AiPermissionRequest{.mode = context.permissionMode,
                            .write = true,
                            .schemaValid = schemaValid,
                            .scopeValid = scopeValid,
                            .explicitDeny = rule.has_value() && rule->disposition == AiPermissionDisposition::deny,
                            .explicitAsk = rule.has_value() && rule->disposition == AiPermissionDisposition::ask,
                            .explicitAllow = rule.has_value() && rule->disposition == AiPermissionDisposition::allow});
    AiTerminalAction action{.dispatchKey = key,
                            .target = context.target,
                            .kind = runCommand   ? AiTerminalActionKind::runCommand
                                    : writeToPty ? AiTerminalActionKind::writeToPty
                                                 : AiTerminalActionKind::interruptCommand,
                            .command = command,
                            .commandId = commandId,
                            .ptyData = ptyData,
                            .permissionSubject = std::string(permissionSubject),
                            .permissionCapability = capability,
                            .appendEnter = requestedAppendEnter.toBool(),
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
    if (ownership == AiWriteOwnershipResult::userHasControl)
    {
        return fail(QStringLiteral("user_has_control"),
                    QStringLiteral("The user has terminal write control. Resume agent control explicitly first."));
    }
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

AiPermissionDecision AiActionToolDispatcher::permissionDecision(const AiPermissionCapability capability,
                                                                const std::string_view subject,
                                                                const AiActionToolContext &context)
{
    const auto rule = m_permissionRules.evaluate({.capability = capability,
                                                  .subject = subject,
                                                  .sessionId = context.target.sessionId,
                                                  .profileId = context.profileId});
    return m_permissionPolicy.decide(
        AiPermissionRequest{.mode = context.permissionMode,
                            .write = true,
                            .explicitDeny = rule.has_value() && rule->disposition == AiPermissionDisposition::deny,
                            .explicitAsk = rule.has_value() && rule->disposition == AiPermissionDisposition::ask,
                            .explicitAllow = rule.has_value() && rule->disposition == AiPermissionDisposition::allow});
}

bool AiActionToolDispatcher::approve(const AiTerminalAction &action)
{
    return m_ledger.transition(action.dispatchKey, AiToolDispatchState::running);
}

bool AiActionToolDispatcher::deny(const AiTerminalAction &action)
{
    return m_ledger.transition(
        action.dispatchKey, AiToolDispatchState::failed,
        failure(QStringLiteral("permission_denied"), QStringLiteral("The user denied this action.")));
}

bool AiActionToolDispatcher::complete(const AiTerminalAction &action, const AiToolDispatchState state,
                                      std::string resultJson)
{
    return m_ledger.transition(action.dispatchKey, state, std::move(resultJson));
}

bool AiActionToolDispatcher::agentHasControl(const AiSessionTarget &target, const std::string_view conversationId) const
{
    const auto owner = m_ownership.owner(target);
    return owner.has_value() && *owner == conversationId;
}

bool AiActionToolDispatcher::replacePermissionRules(std::vector<AiPermissionRule> rules)
{
    return m_permissionRules.replace(std::move(rules));
}

bool AiActionToolDispatcher::addPermissionRule(AiPermissionRule rule)
{
    return m_permissionRules.add(std::move(rule));
}

bool AiActionToolDispatcher::removePermissionRule(const std::string_view ruleId)
{
    return m_permissionRules.remove(ruleId);
}

const std::vector<AiPermissionRule> &AiActionToolDispatcher::permissionRules() const noexcept
{
    return m_permissionRules.rules();
}

void AiActionToolDispatcher::clearSession(const AiSessionTarget &target)
{
    m_ownership.releaseSession(target);
    m_permissionRules.clearSession(target.sessionId);
}

void AiActionToolDispatcher::clearConversation(const std::string_view conversationId)
{
    m_ownership.releaseConversation(conversationId);
    m_ledger.clearConversation(conversationId);
}

} // namespace ztermy::ai
