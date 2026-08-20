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
    return {begin, begin + value.size()};
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
    void readsCurrentSessionInfo();
    void readsBoundedTerminalRangesWithoutSplittingUtf8();
    void readsBoundedCommandBlocks();
    void readsCommandOutputWithExplicitCursorGaps();
    void readsLiveArtifactForCommandCreatedDuringTurn();
};

void AiReadToolsTests::readsCurrentSessionInfo()
{
    const AiReadTools tools;
    const auto info = tools.readSessionInfo(session("session-1", 3));
    QCOMPARE(info.title, std::string("test host"));
    QCOMPARE(info.host, std::string("host.test"));
    QCOMPARE(info.shell, std::string("bash"));
    QCOMPARE(info.workingDirectory, std::string("/srv"));
    QVERIFY(info.connected);
    QCOMPARE(info.commandBlockCount, std::size_t{1});
}

void AiReadToolsTests::readsBoundedTerminalRangesWithoutSplittingUtf8()
{
    const AiReadTools tools(AiReadToolLimits{.maxTerminalLines = 2, .maxTerminalBytes = 8});
    const auto currentSession = session("session-1", 3);

    const auto range = tools.readTerminal(currentSession, 0, 2);
    QVERIFY(range.has_value());
    QCOMPARE(range->content, std::string("one\n二\n"));
    QCOMPARE(range->lineCount, std::size_t{2});
    QCOMPARE(range->nextLine, std::size_t{2});
    QVERIFY(range->hasMore);
    QVERIFY(range->untrustedEvidence);

    const auto oversized = tools.readTerminal(currentSession, 0, 3);
    QVERIFY(!oversized.has_value());
    QCOMPARE(oversized.error().code, AiReadToolErrorCode::invalidArguments);
}

void AiReadToolsTests::readsBoundedCommandBlocks()
{
    const AiReadTools tools(AiReadToolLimits{.maxCommandOutputBytes = 7});
    const auto currentSession = session("session-1", 3);

    const auto block = tools.readCommandBlock(currentSession, 7);
    QVERIFY(block.has_value());
    QCOMPARE(block->command, std::string("false"));
    QCOMPARE(block->output, std::string("failure"));
    QCOMPARE(block->exitStatus, std::optional<int>{1});
    QVERIFY(block->truncated);
    QVERIFY(block->untrustedEvidence);

    const auto missing = tools.readCommandBlock(currentSession, 8);
    QVERIFY(!missing.has_value());
    QCOMPARE(missing.error().code, AiReadToolErrorCode::commandBlockNotFound);
}

void AiReadToolsTests::readsCommandOutputWithExplicitCursorGaps()
{
    const AiReadTools tools(AiReadToolLimits{.maxCommandOutputBytes = 16});
    auto snapshot = session("session-1", 3);
    auto &block = snapshot.commandBlocks.front();
    block.retainedOutput = bytes("headtail");
    block.retainedHeadBytes = 4;
    block.firstOutputStreamOffset = 100;
    block.retainedTailStreamOffset = 196;
    block.nextOutputStreamOffset = 200;
    block.observedOutputBytes = 100;
    block.omittedOutputBytes = 92;
    const auto head = tools.readCommandOutput(snapshot, 7, 100, 2);
    QVERIFY(head.has_value());
    QCOMPARE(head->output, std::string("he"));
    QCOMPARE(head->nextCursor, std::uint64_t{102});
    QVERIFY(head->hasMore);
    QVERIFY(head->truncated);

    const auto tail = tools.readCommandOutput(snapshot, 7, 104, 16);
    QVERIFY(tail.has_value());
    QCOMPARE(tail->output, std::string("tail"));
    QCOMPARE(tail->skippedBytes, std::uint64_t{92});
    QCOMPARE(tail->nextCursor, std::uint64_t{200});
    QVERIFY(!tail->hasMore);

    const auto expired = tools.readCommandOutput(snapshot, 7, 150, 16);
    QVERIFY(!expired.has_value());
    QCOMPARE(expired.error().code, AiReadToolErrorCode::cursorExpired);
    QCOMPARE(expired.error().nextAvailableCursor, std::optional<std::uint64_t>{196});

    const auto end = tools.readCommandOutput(snapshot, 7, 200, 16);
    QVERIFY(end.has_value());
    QVERIFY(end->output.empty());
    QVERIFY(!end->hasMore);

    auto unicodeSnapshot = session("session-2", 1);
    auto &unicodeBlock = unicodeSnapshot.commandBlocks.front();
    unicodeBlock.retainedOutput = bytes("二");
    unicodeBlock.retainedHeadBytes = unicodeBlock.retainedOutput.size();
    unicodeBlock.firstOutputStreamOffset = 0;
    unicodeBlock.nextOutputStreamOffset = 3;
    unicodeBlock.retainedTailStreamOffset = 3;
    const auto split = tools.readCommandOutput(unicodeSnapshot, 7, 0, 1);
    QVERIFY(!split.has_value());
    QCOMPARE(split.error().code, AiReadToolErrorCode::invalidArguments);
}

void AiReadToolsTests::readsLiveArtifactForCommandCreatedDuringTurn()
{
    ztermy::terminal::CommandBlockStore store(ztermy::terminal::CommandBlockStoreLimits{
        .maxOutputBytesPerBlock = 8,
        .retainedHeadBytes = 2,
        .maxArtifactBytesPerBlock = 64,
        .maxArtifactBytesTotal = 64,
    });
    const auto id = store.begin(ztermy::terminal::CommandBlockStart{.command = "generated during turn"});
    QVERIFY(id.has_value());
    const auto output = bytes("0123456789ABCDEF");
    QVERIFY(store.append(*id, {.bytes = std::span(output), .streamOffset = 0}).has_value());
    QVERIFY(store.finish(*id, 0, 100).has_value());

    auto snapshot = session("session-1", 3);
    snapshot.commandOutputReader = [&store](const auto blockId, const auto cursor, const auto maximumBytes) {
        return store.readOutputArtifact(blockId, cursor, maximumBytes);
    };
    const AiReadTools tools(AiReadToolLimits{.maxCommandOutputBytes = 16});
    const auto middle = tools.readCommandOutput(snapshot, *id, 4, 6);
    QVERIFY(middle.has_value());
    QCOMPARE(middle->output, std::string("456789"));
    QCOMPARE(middle->nextCursor, std::uint64_t{10});
    QCOMPARE(middle->artifactRetainedBytes, std::uint64_t{16});
    QCOMPARE(middle->artifactOmittedBytes, std::uint64_t{0});
    QVERIFY(middle->artifactBacked);
    QVERIFY(middle->artifactComplete);
    QVERIFY(middle->streamHasMore);
}

} // namespace

QTEST_GUILESS_MAIN(AiReadToolsTests)

#include "ai_read_tools_tests.moc"
