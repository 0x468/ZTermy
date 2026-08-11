#include "domain/ai/AiAgentGuard.h"

#include <QtTest/QTest>

#include <chrono>

namespace
{

using ztermy::ai::AiAgentBudgetDecision;
using ztermy::ai::AiAgentTurnBudget;
using ztermy::ai::AiAgentTurnLimits;
using ztermy::ai::AiSessionTarget;
using ztermy::ai::AiSessionWriteOwnership;
using ztermy::ai::AiWriteOwnershipResult;

class AiAgentGuardTests final : public QObject
{
    Q_OBJECT

private slots:
    void grantsOneWriteOwnerPerSessionGeneration();
    void transfersAndReleasesOwnershipExplicitly();
    void enforcesToolAndWriteBudgets();
    void detectsReadLoopsOnlyWithoutStateProgress();
    void enforcesTimeAndTokenBudgets();
};

void AiAgentGuardTests::grantsOneWriteOwnerPerSessionGeneration()
{
    AiSessionWriteOwnership ownership;
    const AiSessionTarget target{.sessionId = "session-1", .sessionGeneration = 4};
    QCOMPARE(ownership.claim(target, "conversation-1"), AiWriteOwnershipResult::acquired);
    QCOMPARE(ownership.claim(target, "conversation-1"), AiWriteOwnershipResult::alreadyOwned);
    QCOMPARE(ownership.claim(target, "conversation-2"), AiWriteOwnershipResult::conflict);
    QCOMPARE(ownership.owner(target), std::optional<std::string>{"conversation-1"});

    const AiSessionTarget reconnected{.sessionId = "session-1", .sessionGeneration = 5};
    QCOMPARE(ownership.claim(reconnected, "conversation-2"), AiWriteOwnershipResult::acquired);
}

void AiAgentGuardTests::transfersAndReleasesOwnershipExplicitly()
{
    AiSessionWriteOwnership ownership;
    const AiSessionTarget first{.sessionId = "session-1", .sessionGeneration = 4};
    const AiSessionTarget second{.sessionId = "session-2", .sessionGeneration = 1};
    QCOMPARE(ownership.claim(first, "conversation-1"), AiWriteOwnershipResult::acquired);
    QCOMPARE(ownership.claim(second, "conversation-1"), AiWriteOwnershipResult::acquired);
    QVERIFY(!ownership.transfer(first, "wrong-owner", "conversation-2"));
    QVERIFY(ownership.transfer(first, "conversation-1", "conversation-2"));
    QCOMPARE(ownership.owner(first), std::optional<std::string>{"conversation-2"});

    ownership.releaseConversation("conversation-1");
    QVERIFY(!ownership.owner(second).has_value());
    QVERIFY(ownership.owner(first).has_value());
    ownership.releaseSession(first);
    QVERIFY(!ownership.owner(first).has_value());
}

void AiAgentGuardTests::enforcesToolAndWriteBudgets()
{
    const auto start = AiAgentTurnBudget::Clock::now();
    AiAgentTurnBudget budget(AiAgentTurnLimits{.maximumToolCalls = 3, .maximumWriteActions = 1}, start);
    QCOMPARE(budget.authorize(false, "read:a", 1, start), AiAgentBudgetDecision::allow);
    QCOMPARE(budget.authorize(true, {}, 1, start), AiAgentBudgetDecision::allow);
    QCOMPARE(budget.authorize(true, {}, 1, start), AiAgentBudgetDecision::writeActionLimit);
    QCOMPARE(budget.authorize(false, "read:b", 1, start), AiAgentBudgetDecision::allow);
    QCOMPARE(budget.authorize(false, "read:c", 1, start), AiAgentBudgetDecision::toolCallLimit);
    QCOMPARE(budget.toolCalls(), std::uint32_t{3});
    QCOMPARE(budget.writeActions(), std::uint32_t{1});
}

void AiAgentGuardTests::detectsReadLoopsOnlyWithoutStateProgress()
{
    const auto start = AiAgentTurnBudget::Clock::now();
    AiAgentTurnBudget budget(AiAgentTurnLimits{.maximumRepeatedReads = 2}, start);
    QCOMPARE(budget.authorize(false, "read:session", 7, start), AiAgentBudgetDecision::allow);
    QCOMPARE(budget.authorize(false, "read:other", 7, start), AiAgentBudgetDecision::allow);
    QCOMPARE(budget.authorize(false, "read:session", 7, start), AiAgentBudgetDecision::allow);
    QCOMPARE(budget.authorize(true, {}, 7, start), AiAgentBudgetDecision::allow);
    QCOMPARE(budget.authorize(false, "read:session", 7, start), AiAgentBudgetDecision::repeatedReadLimit);
    QCOMPARE(budget.authorize(false, "read:session", 8, start), AiAgentBudgetDecision::allow);
    QCOMPARE(budget.authorize(false, "read:other", 7, start), AiAgentBudgetDecision::allow);
}

void AiAgentGuardTests::enforcesTimeAndTokenBudgets()
{
    using namespace std::chrono_literals;
    const auto start = AiAgentTurnBudget::Clock::now();
    AiAgentTurnBudget budget(AiAgentTurnLimits{.maximumTokenUsage = 100, .maximumDuration = 10s}, start);
    QCOMPARE(budget.observeTokenUsage(100), AiAgentBudgetDecision::allow);
    QCOMPARE(budget.observeTokenUsage(101), AiAgentBudgetDecision::tokenLimit);
    QCOMPARE(budget.authorize(false, {}, 0, start + 10s), AiAgentBudgetDecision::allow);
    QCOMPARE(budget.authorize(false, {}, 0, start + 10s + 1ms), AiAgentBudgetDecision::timeLimit);
    QCOMPARE(budget.authorize(false, {}, 0, start - 1ms), AiAgentBudgetDecision::timeLimit);
}

} // namespace

QTEST_GUILESS_MAIN(AiAgentGuardTests)

#include "ai_agent_guard_tests.moc"
