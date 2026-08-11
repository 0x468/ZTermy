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

[[nodiscard]] QJsonObject object(const std::string &value)
{
    return QJsonDocument::fromJson(QByteArray::fromStdString(value)).object();
}

class AiActionToolDispatcherTests final : public QObject
{
    Q_OBJECT

private slots:
    void publishesStrictRunCommandDefinition();
    void waitsForApprovalAndCachesCompletion();
    void rejectsScopeReplayAndObserverWrites();
    void appliesRiskBudgetAndOwnershipGuards();
};

void AiActionToolDispatcherTests::publishesStrictRunCommandDefinition()
{
    const auto definitions = AiActionToolDispatcher::definitions();
    QCOMPARE(definitions.size(), std::size_t{1});
    QCOMPARE(definitions.front().name, std::string("run_command"));
    const auto schema = QJsonDocument::fromJson(QByteArray::fromStdString(definitions.front().parametersJson));
    QVERIFY(schema.isObject());
    QVERIFY(!schema.object().value(QStringLiteral("additionalProperties")).toBool(true));
}

void AiActionToolDispatcherTests::waitsForApprovalAndCachesCompletion()
{
    AiActionToolDispatcher dispatcher;
    AiAgentTurnBudget budget;
    const auto call = commandCall();
    const auto plan = dispatcher.prepare(call, context(), budget);
    QCOMPARE(plan.disposition, AiActionToolDisposition::awaitApproval);
    QVERIFY(plan.runCommand.has_value());
    QVERIFY(plan.sideEffecting);
    const auto action = plan.runCommand.value_or(ztermy::ai::AiRunCommandAction{});
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
    QVERIFY(risky.runCommand.value_or(ztermy::ai::AiRunCommandAction{}).risk.highRisk());
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
