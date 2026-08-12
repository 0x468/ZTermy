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
using ztermy::ai::AiPermissionMode;
using ztermy::ai::AiTerminalAction;
using ztermy::ai::AiTerminalActionKind;
using ztermy::ai::AiToolCall;
using ztermy::ai::AiToolDispatchState;

[[nodiscard]] AiActionToolContext context(const std::string &conversation = "conversation-1",
                                          const AiPermissionMode mode = AiPermissionMode::askEachWrite)
{
    return AiActionToolContext{.conversationId = conversation,
                               .turnId = 7,
                               .target = {.sessionId = "session-1", .sessionGeneration = 3},
                               .permissionMode = mode,
                               .savedHost = true};
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

[[nodiscard]] AiToolCall transferControlCall(const std::string &id = "transfer-1")
{
    return AiToolCall{.id = id,
                      .name = "transfer_control",
                      .argumentsJson = R"({"session_id":"session-1","session_generation":3,"to":"user"})"};
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
    void transfersControlToUserUntilResumed();
    void waitsForApprovalAndCachesCompletion();
    void rejectsScopeReplayAndObserverWrites();
    void appliesRiskBudgetAndOwnershipGuards();
};

void AiActionToolDispatcherTests::publishesStrictRunCommandDefinition()
{
    const auto definitions = AiActionToolDispatcher::definitions();
    QCOMPARE(definitions.size(), std::size_t{4});
    QCOMPARE(definitions.front().name, std::string("run_command"));
    const auto schema = QJsonDocument::fromJson(QByteArray::fromStdString(definitions.front().parametersJson));
    QVERIFY(schema.isObject());
    QVERIFY(!schema.object().value(QStringLiteral("additionalProperties")).toBool(true));
}

void AiActionToolDispatcherTests::transfersControlToUserUntilResumed()
{
    AiActionToolDispatcher dispatcher;
    AiAgentTurnBudget budget;
    const auto transfer =
        dispatcher.prepare(transferControlCall(), context("owner", AiPermissionMode::sessionAuto), budget);
    QCOMPARE(transfer.disposition, AiActionToolDisposition::execute);
    QCOMPARE(transfer.action.value_or(AiTerminalAction{}).kind, AiTerminalActionKind::transferToUser);
    QCOMPARE(budget.toolCalls(), std::uint32_t{1});
    QCOMPARE(budget.writeActions(), std::uint32_t{0});
    QVERIFY(dispatcher.userHasControl({.sessionId = "session-1", .sessionGeneration = 3}, "owner"));

    AiAgentTurnBudget blockedBudget;
    const auto blocked =
        dispatcher.prepare(commandCall("blocked-call"), context("owner", AiPermissionMode::sessionAuto), blockedBudget);
    QCOMPARE(
        object(blocked.outputJson).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
        QStringLiteral("user_has_control"));
    QVERIFY(dispatcher.resumeAgent({.sessionId = "session-1", .sessionGeneration = 3}, "owner"));

    AiAgentTurnBudget resumedBudget;
    const auto resumed =
        dispatcher.prepare(commandCall("resumed-call"), context("owner", AiPermissionMode::sessionAuto), resumedBudget);
    QCOMPARE(resumed.disposition, AiActionToolDisposition::execute);
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
    plan = dispatcher.prepare(commandCall("observer-call"), context("observer", AiPermissionMode::observer),
                              observerBudget);
    QCOMPARE(object(plan.outputJson).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
             QStringLiteral("permission_denied"));
    QCOMPARE(observerBudget.writeActions(), std::uint32_t{0});

    auto unavailableContext = context("unavailable", AiPermissionMode::sessionAuto);
    unavailableContext.writable = false;
    AiAgentTurnBudget unavailableBudget;
    plan = dispatcher.prepare(commandCall("unavailable-call"), unavailableContext, unavailableBudget);
    QCOMPARE(object(plan.outputJson).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
             QStringLiteral("session_unavailable"));
    QCOMPARE(unavailableBudget.writeActions(), std::uint32_t{0});

    AiAgentTurnBudget mismatchBudget;
    const auto mismatch = dispatcher.prepare(commandCall("observer-call", "whoami"),
                                             context("observer", AiPermissionMode::observer), mismatchBudget);
    QCOMPARE(
        object(mismatch.outputJson).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
        QStringLiteral("duplicate_mismatch"));
}

void AiActionToolDispatcherTests::appliesRiskBudgetAndOwnershipGuards()
{
    AiActionToolDispatcher dispatcher;
    AiAgentTurnBudget riskBudget;
    const auto risky = dispatcher.prepare(commandCall("risk-call", "rm -rf /tmp/example"),
                                          context("risk", AiPermissionMode::sessionAuto), riskBudget);
    QCOMPARE(risky.disposition, AiActionToolDisposition::awaitApproval);
    QVERIFY(risky.action.value_or(AiTerminalAction{}).risk.highRisk());
    dispatcher.clearConversation("risk");

    AiAgentTurnBudget ownerBudget;
    const auto owner =
        dispatcher.prepare(commandCall("owner-call"), context("owner", AiPermissionMode::sessionAuto), ownerBudget);
    QCOMPARE(owner.disposition, AiActionToolDisposition::execute);

    AiAgentTurnBudget conflictBudget;
    const auto conflict = dispatcher.prepare(commandCall("conflict-call"),
                                             context("other-owner", AiPermissionMode::sessionAuto), conflictBudget);
    QCOMPARE(
        object(conflict.outputJson).value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
        QStringLiteral("ownership_conflict"));

    AiActionToolDispatcher limitedDispatcher;
    AiAgentTurnBudget limited(AiAgentTurnLimits{.maximumToolCalls = 1, .maximumWriteActions = 0});
    const auto limitedPlan = limitedDispatcher.prepare(commandCall("limited-call"),
                                                       context("limited", AiPermissionMode::sessionAuto), limited);
    QCOMPARE(object(limitedPlan.outputJson)
                 .value(QStringLiteral("error"))
                 .toObject()
                 .value(QStringLiteral("code"))
                 .toString(),
             QStringLiteral("write_action_limit"));
}

} // namespace

QTEST_GUILESS_MAIN(AiActionToolDispatcherTests)

#include "ai_action_tool_dispatcher_tests.moc"
