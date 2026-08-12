#include "domain/terminal/CommandBlockStore.h"

#include <QTest>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace
{

using ztermy::terminal::CommandBlockStart;
using ztermy::terminal::CommandBlockState;
using ztermy::terminal::CommandBlockStore;
using ztermy::terminal::CommandBlockStoreError;
using ztermy::terminal::CommandBlockStoreLimits;
using ztermy::terminal::CommandBoundaryConfidence;
using ztermy::terminal::CommandOutputCoverage;
using ztermy::terminal::CommandOutputObservation;
using ztermy::terminal::CommandProvenance;
using ztermy::terminal::TerminalSemanticCapability;

[[nodiscard]] std::span<const std::byte> bytes(const std::string_view value)
{
    return std::as_bytes(std::span(value));
}

[[nodiscard]] std::string text(const std::span<const std::byte> value)
{
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}

class CommandBlockStoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void recordsExactCommandLifecycle();
    void retainsBoundedHeadAndTail();
    void marksMissingOutputExplicitly();
    void deduplicatesOverlappingObservations();
    void preservesInterleavedEvidence();
    void evictsOnlyFinishedBlocks();
};

void CommandBlockStoreTests::recordsExactCommandLifecycle()
{
    CommandBlockStore store;
    const auto id = store.begin(CommandBlockStart{
        .command = "git status",
        .workingDirectory = "C:/work",
        .sessionId = "local-1",
        .host = "localhost",
        .shell = "pwsh",
        .sessionGeneration = 7,
        .capability = TerminalSemanticCapability::rich,
        .commandProvenance = CommandProvenance::verifiedShellIntegration,
        .boundaryConfidence = CommandBoundaryConfidence::exact,
        .startedUtcMs = 100,
        .outputStreamOffset = 40,
    });
    QVERIFY(id.has_value());

    constexpr std::string_view output = "clean\r\n";
    QVERIFY(store.append(*id, CommandOutputObservation{.bytes = bytes(output), .streamOffset = 40}).has_value());
    QVERIFY(store.finish(*id, 0, 200).has_value());

    const auto *block = store.find(*id);
    QVERIFY(block != nullptr);
    QCOMPARE(block->command, std::string("git status"));
    QCOMPARE(block->sessionId, std::string("local-1"));
    QCOMPARE(block->sessionGeneration, std::uint64_t{7});
    QCOMPARE(block->capability, TerminalSemanticCapability::rich);
    QCOMPARE(block->commandProvenance, CommandProvenance::verifiedShellIntegration);
    QCOMPARE(block->state, CommandBlockState::finished);
    QCOMPARE(block->exitStatus, std::optional<int>(0));
    QCOMPARE(block->outputCoverage, CommandOutputCoverage::complete);
    QCOMPARE(text(block->retainedHead()), std::string(output));
    QVERIFY(block->retainedTail().empty());
    QVERIFY(block->hasCompleteOutput());
}

void CommandBlockStoreTests::retainsBoundedHeadAndTail()
{
    CommandBlockStore store(CommandBlockStoreLimits{
        .maxBlocks = 4,
        .maxOutputBytesPerBlock = 10,
        .retainedHeadBytes = 4,
    });
    const auto id = store.begin(CommandBlockStart{});
    QVERIFY(id.has_value());

    constexpr std::string_view first = "0123456789ABC";
    QVERIFY(store.append(*id, {.bytes = bytes(first), .streamOffset = 0}).has_value());
    constexpr std::string_view second = "DEF";
    QVERIFY(store.append(*id, {.bytes = bytes(second), .streamOffset = first.size()}).has_value());
    QVERIFY(store.finish(*id, 0, 20).has_value());

    const auto *block = store.find(*id);
    QVERIFY(block != nullptr);
    QCOMPARE(block->outputCoverage, CommandOutputCoverage::boundedHeadTail);
    QCOMPARE(block->state, CommandBlockState::finished);
    QCOMPARE(block->observedOutputBytes, std::uint64_t{16});
    QCOMPARE(block->omittedOutputBytes, std::uint64_t{6});
    QCOMPARE(text(block->retainedHead()), std::string("0123"));
    QCOMPARE(text(block->retainedTail()), std::string("ABCDEF"));
    QVERIFY(!block->hasCompleteOutput());
}

void CommandBlockStoreTests::marksMissingOutputExplicitly()
{
    CommandBlockStore store;
    const auto id = store.begin(CommandBlockStart{.outputStreamOffset = 100});
    QVERIFY(id.has_value());
    QVERIFY(store.append(*id, {.bytes = bytes("abc"), .streamOffset = 100}).has_value());
    QVERIFY(store.append(*id, {.bytes = bytes("xyz"), .streamOffset = 107}).has_value());

    const auto *block = store.find(*id);
    QVERIFY(block != nullptr);
    QCOMPARE(block->outputCoverage, CommandOutputCoverage::gapped);
    QCOMPARE(block->missingOutputBytes, std::uint64_t{4});
    QCOMPARE(block->outputGaps.size(), std::size_t{1});
    QCOMPARE(block->outputGaps.front().beginStreamOffset, std::uint64_t{103});
    QCOMPARE(block->outputGaps.front().endStreamOffset, std::uint64_t{107});
    QCOMPARE(block->observedOutputBytes, std::uint64_t{6});
    QCOMPARE(block->nextOutputStreamOffset, std::uint64_t{110});
    QCOMPARE(text(block->retainedHead()), std::string("abcxyz"));
}

void CommandBlockStoreTests::deduplicatesOverlappingObservations()
{
    CommandBlockStore store;
    const auto id = store.begin(CommandBlockStart{.outputStreamOffset = 10});
    QVERIFY(id.has_value());
    QVERIFY(store.append(*id, {.bytes = bytes("abc"), .streamOffset = 10}).has_value());
    QVERIFY(store.append(*id, {.bytes = bytes("bcde"), .streamOffset = 11}).has_value());
    QVERIFY(store.append(*id, {.bytes = bytes("abc"), .streamOffset = 10}).has_value());

    const auto *block = store.find(*id);
    QVERIFY(block != nullptr);
    QCOMPARE(block->observedOutputBytes, std::uint64_t{5});
    QCOMPARE(block->missingOutputBytes, std::uint64_t{0});
    QCOMPARE(text(block->retainedHead()), std::string("abcde"));
}

void CommandBlockStoreTests::preservesInterleavedEvidence()
{
    CommandBlockStore store;
    const auto id = store.begin(CommandBlockStart{});
    QVERIFY(id.has_value());
    QVERIFY(
        store.append(*id, {.bytes = bytes("background output"), .streamOffset = 0, .interleaved = true}).has_value());

    const auto *block = store.find(*id);
    if (block == nullptr)
    {
        QFAIL("The retained command block was not found.");
    }
    QVERIFY(block->hasInterleavedOutput);
    QCOMPARE(block->outputCoverage, CommandOutputCoverage::interleaved);
}

void CommandBlockStoreTests::evictsOnlyFinishedBlocks()
{
    CommandBlockStore store(CommandBlockStoreLimits{.maxBlocks = 2});
    const auto first = store.begin(CommandBlockStart{.command = "first"});
    const auto second = store.begin(CommandBlockStart{.command = "second"});
    QVERIFY(first.has_value());
    QVERIFY(second.has_value());

    const auto full = store.begin(CommandBlockStart{.command = "third"});
    QVERIFY(!full.has_value());
    QCOMPARE(full.error(), CommandBlockStoreError::capacityExceeded);

    QVERIFY(store.finish(*first, 0, 10).has_value());
    const auto third = store.begin(CommandBlockStart{.command = "third"});
    QVERIFY(third.has_value());
    QVERIFY(store.find(*first) == nullptr);
    QVERIFY(store.find(*second) != nullptr);
    QVERIFY(store.find(*third) != nullptr);
}

} // namespace

QTEST_GUILESS_MAIN(CommandBlockStoreTests)

#include "command_block_store_tests.moc"
