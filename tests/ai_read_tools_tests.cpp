#include "domain/ai/AiReadTools.h"

#include <QtTest/QTest>

#include <cstddef>
#include <string_view>
#include <vector>

namespace
{

using ztermy::ai::AiReadToolErrorCode;
using ztermy::ai::AiReadToolLimits;
using ztermy::ai::AiReadTools;
using ztermy::ai::AiTerminalReadSnapshot;
using ztermy::terminal::CommandBlock;
using ztermy::terminal::CommandBlockState;
using ztermy::terminal::CommandOutputCoverage;
using ztermy::terminal::TerminalSemanticCapability;

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view value)
{
    const auto *begin = reinterpret_cast<const std::byte *>(value.data());
    return std::vector<std::byte>(begin, begin + value.size());
}

[[nodiscard]] AiTerminalReadSnapshot session(const std::string &sessionId, const std::uint64_t generation)
{
    CommandBlock block{.id = 7,
                       .command = "false",
                       .workingDirectory = "/srv",
                       .sessionId = sessionId,
                       .host = "host.test",
                       .shell = "bash",
                       .sessionGeneration = generation,
                       .capability = TerminalSemanticCapability::rich,
                       .state = CommandBlockState::finished,
                       .outputCoverage = CommandOutputCoverage::boundedHeadTail,
                       .exitStatus = 1,
                       .retainedOutput = bytes("failure output"),
                       .observedOutputBytes = 64,
                       .omittedOutputBytes = 50};
    return AiTerminalReadSnapshot{.sessionId = sessionId,
                                  .title = "test host",
                                  .host = "host.test",
                                  .shell = "bash",
                                  .workingDirectory = "/srv",
                                  .terminalFrame = "one\n二\nthree\n",
                                  .sessionGeneration = generation,
                                  .capability = TerminalSemanticCapability::rich,
                                  .connected = true,
                                  .commandBlocks = {std::move(block)}};
}

class AiReadToolsTests final : public QObject
{
    Q_OBJECT

private slots:
    void listsBoundedSessionMetadata();
    void rejectsWrongAndStaleSessions();
    void readsBoundedTerminalRangesWithoutSplittingUtf8();
    void readsOnlyBlocksOwnedByTargetGeneration();
};

void AiReadToolsTests::listsBoundedSessionMetadata()
{
    const AiReadTools tools(AiReadToolLimits{.maxSessions = 1});
    const std::vector sessions{session("session-1", 3)};
    const auto summaries = tools.listSessions(sessions);
    QVERIFY(summaries.has_value());
    QCOMPARE(summaries->size(), std::size_t{1});
    QCOMPARE(summaries->front().sessionId, std::string("session-1"));
    QCOMPARE(summaries->front().sessionGeneration, std::uint64_t{3});
    QCOMPARE(summaries->front().commandBlockCount, std::size_t{1});

    const std::vector tooMany{session("session-1", 3), session("session-2", 1)};
    const auto rejected = tools.listSessions(tooMany);
    QVERIFY(!rejected.has_value());
    QCOMPARE(rejected.error().code, AiReadToolErrorCode::limitExceeded);
}

void AiReadToolsTests::rejectsWrongAndStaleSessions()
{
    const AiReadTools tools;
    const std::vector sessions{session("session-1", 3)};

    const auto missing = tools.readSessionInfo(sessions, "other", 3);
    QVERIFY(!missing.has_value());
    QCOMPARE(missing.error().code, AiReadToolErrorCode::sessionNotFound);

    const auto stale = tools.readSessionInfo(sessions, "session-1", 2);
    QVERIFY(!stale.has_value());
    QCOMPARE(stale.error().code, AiReadToolErrorCode::staleSessionGeneration);
}

void AiReadToolsTests::readsBoundedTerminalRangesWithoutSplittingUtf8()
{
    const AiReadTools tools(AiReadToolLimits{.maxTerminalLines = 2, .maxTerminalBytes = 8});
    const std::vector sessions{session("session-1", 3)};

    const auto range = tools.readTerminal(sessions, "session-1", 3, 0, 2);
    QVERIFY(range.has_value());
    QCOMPARE(range->content, std::string("one\n二\n"));
    QCOMPARE(range->lineCount, std::size_t{2});
    QCOMPARE(range->nextLine, std::size_t{2});
    QVERIFY(range->hasMore);
    QVERIFY(range->untrustedEvidence);

    const auto oversized = tools.readTerminal(sessions, "session-1", 3, 0, 3);
    QVERIFY(!oversized.has_value());
    QCOMPARE(oversized.error().code, AiReadToolErrorCode::invalidArguments);
}

void AiReadToolsTests::readsOnlyBlocksOwnedByTargetGeneration()
{
    const AiReadTools tools(AiReadToolLimits{.maxCommandOutputBytes = 7});
    const std::vector sessions{session("session-1", 3)};

    const auto block = tools.readCommandBlock(sessions, "session-1", 3, 7);
    QVERIFY(block.has_value());
    QCOMPARE(block->command, std::string("false"));
    QCOMPARE(block->output, std::string("failure"));
    QCOMPARE(block->exitStatus, std::optional<int>{1});
    QVERIFY(block->truncated);
    QVERIFY(block->untrustedEvidence);

    const auto missing = tools.readCommandBlock(sessions, "session-1", 3, 8);
    QVERIFY(!missing.has_value());
    QCOMPARE(missing.error().code, AiReadToolErrorCode::commandBlockNotFound);

    const auto stale = tools.readCommandBlock(sessions, "session-1", 4, 7);
    QVERIFY(!stale.has_value());
    QCOMPARE(stale.error().code, AiReadToolErrorCode::staleSessionGeneration);
}

} // namespace

QTEST_GUILESS_MAIN(AiReadToolsTests)

#include "ai_read_tools_tests.moc"
