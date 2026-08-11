#include "domain/ai/AiCommandTracker.h"

#include <QtTest/QTest>

#include <array>
#include <string>

namespace
{

using ztermy::ai::AiCommandTracker;
using ztermy::ai::AiTrackedCommand;
using ztermy::ai::AiTrackedCommandState;
using ztermy::terminal::CommandBlock;
using ztermy::terminal::CommandBlockState;
using ztermy::terminal::CommandCompletionReason;

[[nodiscard]] AiTrackedCommand command(const std::string &id = "command-1")
{
    return AiTrackedCommand{.id = id,
                            .conversationId = "conversation-1",
                            .target = {.sessionId = "session-1", .sessionGeneration = 3},
                            .command = "printf hello",
                            .baselineBlockId = 8};
}

[[nodiscard]] CommandBlock block(const std::string &text = "printf hello")
{
    return CommandBlock{.id = 9,
                        .command = text,
                        .sessionId = "session-1",
                        .sessionGeneration = 3,
                        .state = CommandBlockState::running};
}

class AiCommandTrackerTests final : public QObject
{
    Q_OBJECT

private slots:
    void linksOnlyExactPostBaselineBlocks();
    void exposesCompletionDisconnectAndInterruptIntent();
    void evictsOnlyTerminalCommands();
};

void AiCommandTrackerTests::linksOnlyExactPostBaselineBlocks()
{
    AiCommandTracker tracker;
    QVERIFY(tracker.accept(command()));

    auto unrelated = block("echo unrelated");
    const auto queued = tracker.observe("command-1", std::span{&unrelated, std::size_t{1}}, true);
    QVERIFY(queued.has_value());
    const auto queuedValue = queued.value_or(AiTrackedCommand{});
    QCOMPARE(queuedValue.state, AiTrackedCommandState::queued);
    QVERIFY(!queuedValue.blockId.has_value());

    auto exact = block();
    const auto running = tracker.observe("command-1", std::span{&exact, std::size_t{1}}, true);
    QVERIFY(running.has_value());
    const auto runningValue = running.value_or(AiTrackedCommand{});
    QCOMPARE(runningValue.state, AiTrackedCommandState::running);
    QCOMPARE(runningValue.blockId, std::optional<ztermy::terminal::CommandBlockId>{9});

    const std::array<CommandBlock, 0> evicted{};
    const auto unknown = tracker.observe("command-1", evicted, true);
    QVERIFY(unknown.has_value());
    QCOMPARE(unknown.value_or(AiTrackedCommand{}).state, AiTrackedCommandState::outcomeUnknown);
}

void AiCommandTrackerTests::exposesCompletionDisconnectAndInterruptIntent()
{
    AiCommandTracker tracker;
    QVERIFY(tracker.accept(command()));
    QVERIFY(tracker.markInterruptRequested("command-1"));
    auto finished = block();
    finished.state = CommandBlockState::finished;
    finished.exitStatus = 130;
    finished.completionReason = CommandCompletionReason::shellMarker;
    const auto result = tracker.observe("command-1", std::span{&finished, std::size_t{1}}, true);
    QVERIFY(result.has_value());
    const auto resultValue = result.value_or(AiTrackedCommand{});
    QCOMPARE(resultValue.state, AiTrackedCommandState::finished);
    QCOMPARE(resultValue.exitStatus, std::optional<int>{130});
    QVERIFY(resultValue.interruptRequested);
    QVERIFY(!tracker.markInterruptRequested("command-1"));

    AiCommandTracker disconnectedTracker;
    QVERIFY(disconnectedTracker.accept(command("command-2")));
    const std::array<CommandBlock, 0> none{};
    const auto disconnected = disconnectedTracker.observe("command-2", none, false);
    QVERIFY(disconnected.has_value());
    QCOMPARE(disconnected.value_or(AiTrackedCommand{}).state, AiTrackedCommandState::disconnected);
}

void AiCommandTrackerTests::evictsOnlyTerminalCommands()
{
    AiCommandTracker tracker(1);
    QVERIFY(tracker.accept(command()));
    QVERIFY(!tracker.accept(command("command-2")));

    auto finished = block();
    finished.state = CommandBlockState::finished;
    finished.exitStatus = 0;
    static_cast<void>(tracker.observe("command-1", std::span{&finished, std::size_t{1}}, true));
    QVERIFY(tracker.accept(command("command-2")));
    QCOMPARE(tracker.size(), std::size_t{1});
    QVERIFY(!tracker.find("command-1").has_value());
    QVERIFY(tracker.find("command-2").has_value());
}

} // namespace

QTEST_GUILESS_MAIN(AiCommandTrackerTests)

#include "ai_command_tracker_tests.moc"
