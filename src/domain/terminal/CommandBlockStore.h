#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ztermy::terminal
{

using CommandBlockId = std::uint64_t;

enum class CommandBoundaryConfidence : std::uint8_t
{
    exact,
    heuristic,
    unknown,
};

enum class TerminalSemanticCapability : std::uint8_t
{
    none,
    basic,
    rich,
};

enum class CommandProvenance : std::uint8_t
{
    unknown,
    heuristicInput,
    osc133,
    osc633,
    verifiedShellIntegration,
};

enum class CommandBlockState : std::uint8_t
{
    running,
    finished,
};

enum class CommandOutputCoverage : std::uint8_t
{
    complete,
    boundedHeadTail,
    gapped,
    interleaved,
    unknown,
};

enum class CommandBlockStoreError : std::uint8_t
{
    capacityExceeded,
    blockNotFound,
    blockAlreadyFinished,
};

struct CommandBlockStoreLimits final
{
    std::size_t maxBlocks = 64;
    std::size_t maxOutputBytesPerBlock = std::size_t{64} * 1024;
    std::size_t retainedHeadBytes = std::size_t{16} * 1024;
    std::size_t maxGapRangesPerBlock = 64;
};

struct CommandBlockStart final
{
    std::string command;
    std::string workingDirectory;
    std::string sessionId;
    std::string host;
    std::string shell;
    std::uint64_t sessionGeneration = 0;
    TerminalSemanticCapability capability = TerminalSemanticCapability::none;
    CommandProvenance commandProvenance = CommandProvenance::unknown;
    CommandBoundaryConfidence boundaryConfidence = CommandBoundaryConfidence::unknown;
    std::int64_t startedUtcMs = 0;
    std::uint64_t outputStreamOffset = 0;
};

struct CommandOutputGap final
{
    std::uint64_t beginStreamOffset = 0;
    std::uint64_t endStreamOffset = 0;
};

struct CommandOutputObservation final
{
    std::span<const std::byte> bytes;
    std::uint64_t streamOffset = 0;
    bool interleaved = false;
};

struct CommandBlock final
{
    CommandBlockId id = 0;
    std::string command;
    std::string workingDirectory;
    std::string sessionId;
    std::string host;
    std::string shell;
    std::uint64_t sessionGeneration = 0;
    TerminalSemanticCapability capability = TerminalSemanticCapability::none;
    CommandProvenance commandProvenance = CommandProvenance::unknown;
    CommandBoundaryConfidence boundaryConfidence = CommandBoundaryConfidence::unknown;
    CommandBlockState state = CommandBlockState::running;
    CommandOutputCoverage outputCoverage = CommandOutputCoverage::complete;
    std::int64_t startedUtcMs = 0;
    std::optional<std::int64_t> finishedUtcMs;
    std::optional<int> exitStatus;
    std::vector<std::byte> retainedOutput;
    std::size_t retainedHeadBytes = 0;
    std::uint64_t observedOutputBytes = 0;
    std::uint64_t omittedOutputBytes = 0;
    std::uint64_t missingOutputBytes = 0;
    std::uint64_t firstOutputStreamOffset = 0;
    std::uint64_t nextOutputStreamOffset = 0;
    std::uint64_t retainedTailStreamOffset = 0;
    std::vector<CommandOutputGap> outputGaps;
    bool hasInterleavedOutput = false;

    [[nodiscard]] std::span<const std::byte> retainedHead() const noexcept;
    [[nodiscard]] std::span<const std::byte> retainedTail() const noexcept;
    [[nodiscard]] bool hasCompleteOutput() const noexcept;
};

class CommandBlockStore final
{
public:
    explicit CommandBlockStore(CommandBlockStoreLimits limits = {});

    [[nodiscard]] const CommandBlockStoreLimits &limits() const noexcept;
    [[nodiscard]] const std::deque<CommandBlock> &blocks() const noexcept;
    [[nodiscard]] const CommandBlock *find(CommandBlockId id) const noexcept;

    [[nodiscard]] std::expected<CommandBlockId, CommandBlockStoreError> begin(CommandBlockStart start);
    [[nodiscard]] std::expected<void, CommandBlockStoreError> append(CommandBlockId id,
                                                                     CommandOutputObservation observation);
    [[nodiscard]] std::expected<void, CommandBlockStoreError> finish(CommandBlockId id, std::optional<int> exitStatus,
                                                                     std::int64_t finishedUtcMs);
    void clear() noexcept;

private:
    [[nodiscard]] CommandBlock *findMutable(CommandBlockId id) noexcept;
    [[nodiscard]] bool evictOldestFinished();
    void retain(CommandBlock &block, std::span<const std::byte> bytes);
    void recordGap(CommandBlock &block, std::uint64_t begin, std::uint64_t end);
    static void updateCoverage(CommandBlock &block) noexcept;

    CommandBlockStoreLimits m_limits;
    std::deque<CommandBlock> m_blocks;
    CommandBlockId m_nextId = 1;
};

} // namespace ztermy::terminal
