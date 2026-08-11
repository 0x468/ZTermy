#include "domain/ai/AiToolDispatchLedger.h"

#include <QtTest/QTest>

#include <string>
#include <utility>

namespace
{

using ztermy::ai::AiToolDispatchAdmission;
using ztermy::ai::AiToolDispatchKey;
using ztermy::ai::AiToolDispatchLedger;
using ztermy::ai::AiToolDispatchLimits;
using ztermy::ai::AiToolDispatchRequest;
using ztermy::ai::AiToolDispatchState;

[[nodiscard]] AiToolDispatchRequest request(std::uint64_t turn = 1, std::string callId = "call-1")
{
    return {.key = {.conversationId = "conversation-1", .turnId = turn, .toolCallId = std::move(callId)},
            .toolName = "run_command",
            .canonicalArguments = R"({"command":"pwd"})",
            .sessionId = "session-1",
            .sessionGeneration = 4,
            .sideEffecting = true};
}

class AiToolDispatchLedgerTests final : public QObject
{
    Q_OBJECT

private slots:
    void joinsPendingAndCachesCompletedDispatch();
    void rejectsConflictingReplay();
    void enforcesLifecycleAndResultBounds();
    void evictsOnlyFinishedRecords();
    void clearsOnlyRequestedConversation();
};

void AiToolDispatchLedgerTests::joinsPendingAndCachesCompletedDispatch()
{
    AiToolDispatchLedger ledger;
    const auto original = request();
    auto outcome = ledger.begin(original);
    QCOMPARE(outcome.admission, AiToolDispatchAdmission::accepted);
    QVERIFY(outcome.record.has_value());

    outcome = ledger.begin(original);
    QCOMPARE(outcome.admission, AiToolDispatchAdmission::joined);
    QCOMPARE(ledger.size(), std::size_t{1});

    QVERIFY(ledger.transition(original.key, AiToolDispatchState::running));
    QVERIFY(ledger.transition(original.key, AiToolDispatchState::succeeded, R"({"ok":true})"));
    outcome = ledger.begin(original);
    QCOMPARE(outcome.admission, AiToolDispatchAdmission::cached);
    const auto cached = outcome.record.value_or(ztermy::ai::AiToolDispatchRecord{});
    QCOMPARE(cached.resultJson, std::string(R"({"ok":true})"));
}

void AiToolDispatchLedgerTests::rejectsConflictingReplay()
{
    AiToolDispatchLedger ledger;
    const auto original = request();
    QCOMPARE(ledger.begin(original).admission, AiToolDispatchAdmission::accepted);

    auto conflict = original;
    conflict.canonicalArguments = R"({"command":"whoami"})";
    QCOMPARE(ledger.begin(conflict).admission, AiToolDispatchAdmission::duplicateMismatch);

    conflict = original;
    conflict.sessionGeneration = 5;
    QCOMPARE(ledger.begin(conflict).admission, AiToolDispatchAdmission::duplicateMismatch);

    conflict = original;
    conflict.toolName = "write_to_pty";
    QCOMPARE(ledger.begin(conflict).admission, AiToolDispatchAdmission::duplicateMismatch);
}

void AiToolDispatchLedgerTests::enforcesLifecycleAndResultBounds()
{
    AiToolDispatchLedger ledger(AiToolDispatchLimits{.maximumResultBytes = 8});
    const auto original = request();
    QCOMPARE(ledger.begin(original).admission, AiToolDispatchAdmission::accepted);
    QVERIFY(!ledger.transition(original.key, AiToolDispatchState::pending));
    QVERIFY(!ledger.transition(original.key, AiToolDispatchState::running, "premature"));
    QVERIFY(ledger.transition(original.key, AiToolDispatchState::awaitingApproval));
    QVERIFY(ledger.transition(original.key, AiToolDispatchState::running));
    QVERIFY(!ledger.transition(original.key, AiToolDispatchState::succeeded, "123456789"));
    QVERIFY(ledger.transition(original.key, AiToolDispatchState::outcomeUnknown, "unknown"));
    QVERIFY(!ledger.transition(original.key, AiToolDispatchState::succeeded, "done"));
}

void AiToolDispatchLedgerTests::evictsOnlyFinishedRecords()
{
    AiToolDispatchLedger ledger(AiToolDispatchLimits{.maximumRecords = 2});
    const auto first = request(1, "call-1");
    const auto second = request(1, "call-2");
    const auto third = request(1, "call-3");
    QCOMPARE(ledger.begin(first).admission, AiToolDispatchAdmission::accepted);
    QCOMPARE(ledger.begin(second).admission, AiToolDispatchAdmission::accepted);
    QCOMPARE(ledger.begin(third).admission, AiToolDispatchAdmission::capacityExceeded);

    QVERIFY(ledger.transition(first.key, AiToolDispatchState::cancelled, R"({"cancelled":true})"));
    QCOMPARE(ledger.begin(third).admission, AiToolDispatchAdmission::accepted);
    QVERIFY(!ledger.find(first.key).has_value());
    QVERIFY(ledger.find(second.key).has_value());
    QVERIFY(ledger.find(third.key).has_value());
}

void AiToolDispatchLedgerTests::clearsOnlyRequestedConversation()
{
    AiToolDispatchLedger ledger;
    auto first = request(1, "call-1");
    auto second = request(1, "call-2");
    second.key.conversationId = "conversation-2";
    QCOMPARE(ledger.begin(first).admission, AiToolDispatchAdmission::accepted);
    QCOMPARE(ledger.begin(second).admission, AiToolDispatchAdmission::accepted);
    ledger.clearConversation("conversation-1");
    QVERIFY(!ledger.find(first.key).has_value());
    QVERIFY(ledger.find(second.key).has_value());
}

} // namespace

QTEST_GUILESS_MAIN(AiToolDispatchLedgerTests)

#include "ai_tool_dispatch_ledger_tests.moc"
