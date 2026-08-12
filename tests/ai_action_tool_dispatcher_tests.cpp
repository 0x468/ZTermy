#include "application/ai/AiActionToolDispatcher.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QTest>

#include <string>

namespace
{

using ztermy::ai::AiActionToolContext;
using ztermy::ai::AiActionToolDispatcher;
using ztermy::ai::AiActionToolDisposition;
using ztermy::ai::AiAgentTurnBudget;
using ztermy::ai::AiAgentTurnLimits;
using ztermy::ai::AiPermissionCapability;
using ztermy::ai::AiPermissionDisposition;
using ztermy::ai::AiPermissionMode;
using ztermy::ai::AiPermissionRule;
using ztermy::ai::AiPermissionRuleDuration;
using ztermy::ai::AiPermissionRuleMatcher;
using ztermy::ai::AiTerminalAction;
using ztermy::ai::AiTerminalActionKind;
using ztermy::ai::AiToolCall;
using ztermy::ai::AiToolDispatchState;

[[nodiscard]] AiActionToolContext context(const std::string &conversation = "conversation-1",
                                          const AiPermissionMode mode = AiPermissionMode::ask)
{
    return AiActionToolContext{.conversationId = conversation,
                               .turnId = 7,
                               .target = {.sessionId = "session-1", .sessionGeneration = 3},
                               .permissionMode = mode,
                               .profileId = "profile-1"};
}

[[nodiscard]] AiToolCall commandCall(const std::string &id = "call-1", const std::string &command = "pwd")
{
    return AiToolCall{.id = id,
                      .name = "run_command",
                      .argumentsJson = std::string(R"({"session_id":"session-1","session_generation":3,"command":")")
                                       + command + R"("})"};
}

[[nodiscard]] AiToolCall interruptCall(const std::string &id = "interrupt-1")
{
    return AiToolCall{
        .id = id,
        .name = "interrupt_command",
        .argumentsJson = R"({"command_id":"command-1","session_id":"session-1","session_generation":3,"mode":"soft"})"};
}

[[nodiscard]] AiToolCall ptyWriteCall(const std::string &id = "pty-write-1")
{
    return AiToolCall{.id = id,
                      .name = "write_to_pty",
                      .argumentsJson =
                          R"({"session_id":"session-1","session_generation":3,"data":"yes","append_enter":true})"};
}

[[nodiscard]] AiToolCall runbookCall(const std::string &id = "runbook-1")
{
    return AiToolCall{
        .id = id,
        .name = "save_runbook",
        .argumentsJson =
            R"({"session_id":"session-1","session_generation":3,"runbook":{"name":"Inspect host","description":"Inspect disk usage","shell":"bash","steps":[{"command":"df -h","continuation":"immediate","output_marker":"","timeout_ms":30000}]}})"};
}

[[nodiscard]] AiToolCall transferCall(const bool upload, const std::string &id = "sftp-transfer-1")
{
    return AiToolCall{
        .id = id,
        .name = upload ? "queue_sftp_upload" : "queue_sftp_download",
        .argumentsJson =
            R"({"session_id":"session-1","session_generation":3,"local_path":"C:/Temp/report.txt","remote_path":"/tmp/report.txt"})"};
}

[[nodiscard]] QJsonObject object(const std::string &value)
{
    return QJsonDocument::fromJson(QByteArray::fromStdString(value)).object();
}

class AiActionToolDispatcherTests final : public QObject
{
    Q_OBJECT

private slots:
    void publishesStrictRunCommandDefinition();
    void preparesSoftInterruptAsWriteAction();
    void validatesInteractivePtyInput();
    void requiresExplicitApprovalForRunbooks();
    void requiresExplicitApprovalForSftpMutations();
    void waitsForApprovalAndCachesCompletion();
    void rejectsScopeReplayAndObserverWrites();
    void appliesRiskBudgetAndOwnershipGuards();
    void appliesReusableRulesBeforeModeDefaults();
};

void AiActionToolDispatcherTests::publishesStrictRunCommandDefinition()
{
    const auto definitions = AiActionToolDispatcher::definitions();
    QCOMPARE(definitions.size(), std::size_t{6});
    QCOMPARE(definitions.front().name, std::string("run_command"));
    const auto schema = QJsonDocument::fromJson(QByteArray::fromStdString(definitions.front().parametersJson));
    QVERIFY(schema.isObject());
    QVERIFY(!schema.object().value(QStringLiteral("additionalProperties")).toBool(true));
}

void AiActionToolDispatcherTests::requiresExplicitApprovalForSftpMutations()
{
    AiActionToolDispatcher dispatcher;
    for (const bool upload : {false, true})
    {
        AiAgentTurnBudget budget;
        const auto plan = dispatcher.prepare(transferCall(upload, upload ? "upload" : "download"),
                                             context("sftp", AiPermissionMode::automatic), budget);
        QCOMPARE(plan.disposition, AiActionToolDisposition::execute);
        const auto action = plan.action.value_or(AiTerminalAction{});
        QCOMPARE(action.kind,
                 upload ? AiTerminalActionKind::enqueueSftpUpload : AiTerminalActionKind::enqueueSftpDownload);
        QVERIFY(!action.payloadJson.empty());
        QCOMPARE(budget.writeActions(), std::uint32_t{1});
        QVERIFY(!dispatcher.agentHasControl({.sessionId = "session-1", .sessionGeneration = 3}, "sftp"));
    }

    AiAgentTurnBudget observerBudget;
    const auto observer = dispatcher.prepare(transferCall(false, "observer-sftp"),
                                             context("observer-sftp", AiPermissionMode::readOnly), observerBudget);
    QCOMPARE(
        object(observer.outputJson).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
        QStringLiteral("permission_denied"));

    auto unsavedContext = context("unsaved-sftp", AiPermissionMode::automatic);
    unsavedContext.profileId.clear();
    AiAgentTurnBudget unsavedBudget;
    const auto unsaved = dispatcher.prepare(transferCall(true, "unsaved-sftp"), unsavedContext, unsavedBudget);
    QCOMPARE(unsaved.disposition, AiActionToolDisposition::execute);
}

void AiActionToolDispatcherTests::requiresExplicitApprovalForRunbooks()
{
    AiActionToolDispatcher dispatcher;
    AiAgentTurnBudget budget;
    const auto plan = dispatcher.prepare(runbookCall(), context("runbook", AiPermissionMode::automatic), budget);
    QCOMPARE(plan.disposition, AiActionToolDisposition::execute);
    const auto action = plan.action.value_or(AiTerminalAction{});
    QCOMPARE(action.kind, AiTerminalActionKind::saveRunbook);
    QVERIFY(!action.payloadJson.empty());
    QCOMPARE(budget.writeActions(), std::uint32_t{1});
    QVERIFY(!dispatcher.agentHasControl({.sessionId = "session-1", .sessionGeneration = 3}, "runbook"));

    AiAgentTurnBudget observerBudget;
    const auto denied = dispatcher.prepare(runbookCall("runbook-observer"),
                                           context("observer-runbook", AiPermissionMode::readOnly), observerBudget);
    QCOMPARE(
        object(denied.outputJson).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
        QStringLiteral("permission_denied"));

    auto oversized = runbookCall("oversized-runbook");
    QJsonObject arguments = object(oversized.argumentsJson);
    QJsonObject runbook = arguments.value(QStringLiteral("runbook")).toObject();
    runbook.insert(QStringLiteral("name"), QString(129, QLatin1Char('a')));
    arguments.insert(QStringLiteral("runbook"), runbook);
    oversized.argumentsJson = QJsonDocument(arguments).toJson(QJsonDocument::Compact).toStdString();
    AiAgentTurnBudget oversizedBudget;
    const auto rejected = dispatcher.prepare(oversized, context("oversized-runbook"), oversizedBudget);
    QCOMPARE(
        object(rejected.outputJson).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
        QStringLiteral("invalid_arguments"));
    QCOMPARE(oversizedBudget.writeActions(), std::uint32_t{0});
}

void AiActionToolDispatcherTests::validatesInteractivePtyInput()
{
    AiActionToolDispatcher dispatcher;
    AiAgentTurnBudget budget;
    const auto plan = dispatcher.prepare(ptyWriteCall(), context(), budget);
    QCOMPARE(plan.disposition, AiActionToolDisposition::awaitApproval);
    const auto action = plan.action.value_or(AiTerminalAction{});
    QCOMPARE(action.kind, AiTerminalActionKind::writeToPty);
    QCOMPARE(action.ptyData, std::string("yes"));
    QVERIFY(action.appendEnter);

    AiAgentTurnBudget invalidBudget;
    auto invalid = ptyWriteCall("pty-write-invalid");
    invalid.argumentsJson =
        R"({"session_id":"session-1","session_generation":3,"data":"yes\nwhoami","append_enter":true})";
    const auto rejected = dispatcher.prepare(invalid, context(), invalidBudget);
    QCOMPARE(
        object(rejected.outputJson).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
        QStringLiteral("invalid_arguments"));
    QCOMPARE(invalidBudget.writeActions(), std::uint32_t{0});
}

void AiActionToolDispatcherTests::preparesSoftInterruptAsWriteAction()
{
    AiActionToolDispatcher dispatcher;
    AiAgentTurnBudget budget;
    const auto plan = dispatcher.prepare(interruptCall(), context(), budget);
    QCOMPARE(plan.disposition, AiActionToolDisposition::awaitApproval);
    QVERIFY(plan.sideEffecting);
    const auto action = plan.action.value_or(AiTerminalAction{});
    QCOMPARE(action.kind, AiTerminalActionKind::interruptCommand);
    QCOMPARE(action.commandId, std::string("command-1"));
    QCOMPARE(budget.writeActions(), std::uint32_t{1});

    AiAgentTurnBudget invalidBudget;
    auto invalid = interruptCall("interrupt-invalid");
    invalid.argumentsJson =
        R"({"command_id":"command-1","session_id":"session-1","session_generation":3,"mode":"kill"})";
    const auto rejected = dispatcher.prepare(invalid, context(), invalidBudget);
    QCOMPARE(
        object(rejected.outputJson).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
        QStringLiteral("invalid_arguments"));
    QCOMPARE(invalidBudget.writeActions(), std::uint32_t{0});
}

void AiActionToolDispatcherTests::waitsForApprovalAndCachesCompletion()
{
    AiActionToolDispatcher dispatcher;
    AiAgentTurnBudget budget;
    const auto call = commandCall();
    const auto plan = dispatcher.prepare(call, context(), budget);
    QCOMPARE(plan.disposition, AiActionToolDisposition::awaitApproval);
    QVERIFY(plan.action.has_value());
    QVERIFY(plan.sideEffecting);
    const auto action = plan.action.value_or(AiTerminalAction{});
    QCOMPARE(action.command, std::string("pwd"));
    QVERIFY(dispatcher.approve(action));
    const std::string completed = R"({"ok":true,"command_id":"command-1"})";
    QVERIFY(dispatcher.complete(action, AiToolDispatchState::succeeded, completed));

    AiAgentTurnBudget replayBudget;
    const auto replay = dispatcher.prepare(call, context(), replayBudget);
    QCOMPARE(replay.disposition, AiActionToolDisposition::respond);
    QCOMPARE(replay.outputJson, completed);
    QCOMPARE(replayBudget.toolCalls(), std::uint32_t{0});
}

void AiActionToolDispatcherTests::rejectsScopeReplayAndObserverWrites()
{
    AiActionToolDispatcher dispatcher;
    AiAgentTurnBudget budget;
    auto wrongScope = commandCall("scope-call");
    wrongScope.argumentsJson = R"({"session_id":"other","session_generation":3,"command":"pwd"})";
    auto plan = dispatcher.prepare(wrongScope, context(), budget);
    QCOMPARE(object(plan.outputJson).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
             QStringLiteral("scope_changed"));

    AiAgentTurnBudget observerBudget;
    plan = dispatcher.prepare(commandCall("observer-call"), context("observer", AiPermissionMode::readOnly),
                              observerBudget);
    QCOMPARE(object(plan.outputJson).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
             QStringLiteral("permission_denied"));
    QCOMPARE(observerBudget.writeActions(), std::uint32_t{0});

    auto unavailableContext = context("unavailable", AiPermissionMode::automatic);
    unavailableContext.writable = false;
    AiAgentTurnBudget unavailableBudget;
    plan = dispatcher.prepare(commandCall("unavailable-call"), unavailableContext, unavailableBudget);
    QCOMPARE(object(plan.outputJson).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
             QStringLiteral("session_unavailable"));
    QCOMPARE(unavailableBudget.writeActions(), std::uint32_t{0});

    AiAgentTurnBudget mismatchBudget;
    const auto mismatch = dispatcher.prepare(commandCall("observer-call", "whoami"),
                                             context("observer", AiPermissionMode::readOnly), mismatchBudget);
    QCOMPARE(
        object(mismatch.outputJson).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
        QStringLiteral("duplicate_mismatch"));
}

void AiActionToolDispatcherTests::appliesRiskBudgetAndOwnershipGuards()
{
    AiActionToolDispatcher dispatcher;
    AiAgentTurnBudget riskBudget;
    const auto risky = dispatcher.prepare(commandCall("risk-call", "rm -rf /tmp/example"),
                                          context("risk", AiPermissionMode::automatic), riskBudget);
    QCOMPARE(risky.disposition, AiActionToolDisposition::execute);
    QVERIFY(risky.action.value_or(AiTerminalAction{}).risk.highRisk());
    dispatcher.clearConversation("risk");

    AiAgentTurnBudget ownerBudget;
    const auto owner =
        dispatcher.prepare(commandCall("owner-call"), context("owner", AiPermissionMode::automatic), ownerBudget);
    QCOMPARE(owner.disposition, AiActionToolDisposition::execute);

    AiAgentTurnBudget conflictBudget;
    const auto conflict = dispatcher.prepare(commandCall("conflict-call"),
                                             context("other-owner", AiPermissionMode::automatic), conflictBudget);
    QCOMPARE(
        object(conflict.outputJson).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
        QStringLiteral("ownership_conflict"));

    AiActionToolDispatcher limitedDispatcher;
    AiAgentTurnBudget limited(AiAgentTurnLimits{.maximumToolCalls = 1, .maximumWriteActions = 0});
    const auto limitedPlan = limitedDispatcher.prepare(commandCall("limited-call"),
                                                       context("limited", AiPermissionMode::automatic), limited);
    QCOMPARE(object(limitedPlan.outputJson)
                 .value(QStringLiteral("error"))
                 .toObject()
                 .value(QStringLiteral("code"))
                 .toString(),
             QStringLiteral("write_action_limit"));
}

void AiActionToolDispatcherTests::appliesReusableRulesBeforeModeDefaults()
{
    AiActionToolDispatcher dispatcher;
    QVERIFY(dispatcher.addPermissionRule({.id = "allow-status",
                                          .capability = AiPermissionCapability::terminalCommand,
                                          .matcher = AiPermissionRuleMatcher::exact,
                                          .pattern = "git status",
                                          .disposition = AiPermissionDisposition::allow,
                                          .duration = AiPermissionRuleDuration::profile,
                                          .profileId = "profile-1"}));
    AiAgentTurnBudget allowedBudget;
    auto plan = dispatcher.prepare(commandCall("rule-allow", "git status"), context("rule-allow"), allowedBudget);
    QCOMPARE(plan.disposition, AiActionToolDisposition::execute);
    dispatcher.clearConversation("rule-allow");

    QVERIFY(dispatcher.addPermissionRule({.id = "deny-reboot",
                                          .capability = AiPermissionCapability::terminalCommand,
                                          .matcher = AiPermissionRuleMatcher::glob,
                                          .pattern = "*reboot*",
                                          .disposition = AiPermissionDisposition::deny,
                                          .duration = AiPermissionRuleDuration::global}));
    AiAgentTurnBudget deniedBudget;
    plan = dispatcher.prepare(commandCall("rule-deny", "sudo reboot"), context("rule-deny", AiPermissionMode::yolo),
                              deniedBudget);
    QCOMPARE(object(plan.outputJson).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
             QStringLiteral("permission_denied"));

    QVERIFY(dispatcher.addPermissionRule({.id = "ask-downloads",
                                          .capability = AiPermissionCapability::sftpDownload,
                                          .matcher = AiPermissionRuleMatcher::prefix,
                                          .pattern = "/tmp/",
                                          .disposition = AiPermissionDisposition::ask,
                                          .duration = AiPermissionRuleDuration::global}));
    AiAgentTurnBudget askBudget;
    plan = dispatcher.prepare(transferCall(false, "rule-ask"), context("rule-ask", AiPermissionMode::yolo), askBudget);
    QCOMPARE(plan.disposition, AiActionToolDisposition::awaitApproval);

    QVERIFY(dispatcher.addPermissionRule({.id = "allow-runbook",
                                          .capability = AiPermissionCapability::runbookMutation,
                                          .matcher = AiPermissionRuleMatcher::all,
                                          .disposition = AiPermissionDisposition::allow,
                                          .duration = AiPermissionRuleDuration::profile,
                                          .profileId = "profile-1"}));
    AiAgentTurnBudget runbookBudget;
    plan = dispatcher.prepare(runbookCall("rule-runbook"), context("rule-runbook", AiPermissionMode::readOnly),
                              runbookBudget);
    QCOMPARE(plan.disposition, AiActionToolDisposition::execute);
}

} // namespace

QTEST_GUILESS_MAIN(AiActionToolDispatcherTests)

#include "ai_action_tool_dispatcher_tests.moc"
