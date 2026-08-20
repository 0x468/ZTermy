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

enum class CommandCompletionReason : std::uint8_t
{
    shellMarker,
    promptRecovery,
    disconnect,
    decoderReset,
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

enum class CommandOutputArtifactErrorCode : std::uint8_t
{
    blockNotFound,
    cursorExpired,
    artifactExpired,
    rangeOutOfBounds,
    outputUnavailable,
    limitExceeded,
};

struct CommandOutputArtifactError final
{
    CommandOutputArtifactErrorCode code = CommandOutputArtifactErrorCode::blockNotFound;
    std::optional<std::uint64_t> nextAvailableCursor;
};

struct CommandBlockStoreLimits final
{
    std::size_t maxBlocks = 64;
    std::size_t maxOutputBytesPerBlock = std::size_t{64} * 1024;
    std::size_t retainedHeadBytes = std::size_t{16} * 1024;
    std::size_t maxGapRangesPerBlock = 64;
    std::size_t maxArtifactBytesPerBlock = std::size_t{2} * 1024 * 1024;
    std::size_t maxArtifactBytesTotal = std::size_t{8} * 1024 * 1024;
    std::size_t maxArtifactSegmentsPerBlock = 64;
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
    std::optional<CommandCompletionReason> completionReason;
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
    bool outputCoverageUncertain = false;

    [[nodiscard]] std::span<const std::byte> retainedHead() const noexcept;
    [[nodiscard]] std::span<const std::byte> retainedTail() const noexcept;
    [[nodiscard]] bool hasCompleteOutput() const noexcept;
};

struct CommandOutputArtifactPage final
{
    CommandBlockId id = 0;
    CommandBlockState state = CommandBlockState::running;
    CommandOutputCoverage outputCoverage = CommandOutputCoverage::unknown;
    std::optional<int> exitStatus;
    std::vector<std::byte> output;
    std::uint64_t requestedCursor = 0;
    std::uint64_t nextCursor = 0;
    std::uint64_t streamStart = 0;
    std::uint64_t streamEnd = 0;
    std::uint64_t skippedBytes = 0;
    std::uint64_t retainedBytes = 0;
    std::uint64_t omittedBytes = 0;
    bool readableMore = false;
    bool streamHasMore = false;
    bool pageTruncated = false;
    bool artifactComplete = false;
    bool artifactExpired = false;
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
    [[nodiscard]] std::expected<void, CommandBlockStoreError>
    finish(CommandBlockId id, std::optional<int> exitStatus, std::int64_t finishedUtcMs,
           CommandCompletionReason reason = CommandCompletionReason::shellMarker);
    [[nodiscard]] std::expected<void, CommandBlockStoreError> markOutputUnknown(CommandBlockId id);
    [[nodiscard]] std::expected<CommandOutputArtifactPage, CommandOutputArtifactError>
    readOutputArtifact(CommandBlockId id, std::uint64_t afterCursor, std::size_t maximumBytes) const;
    void clear() noexcept;

private:
    struct CommandOutputArtifactSegment final
    {
        std::uint64_t streamOffset = 0;
        std::vector<std::byte> bytes;
    };

    struct CommandOutputArtifact final
    {
        CommandBlockId blockId = 0;
        std::uint64_t streamStart = 0;
        std::uint64_t streamEnd = 0;
        std::uint64_t observedBytes = 0;
        std::uint64_t omittedBytes = 0;
        std::vector<CommandOutputArtifactSegment> segments;
        bool finished = false;
        bool expired = false;
    };

    [[nodiscard]] CommandBlock *findMutable(CommandBlockId id) noexcept;
    [[nodiscard]] CommandOutputArtifact *findArtifactMutable(CommandBlockId id) noexcept;
    [[nodiscard]] const CommandOutputArtifact *findArtifact(CommandBlockId id) const noexcept;
    [[nodiscard]] bool evictOldestFinished();
    void retain(CommandBlock &block, std::span<const std::byte> bytes);
    void retainArtifact(CommandOutputArtifact &artifact, std::uint64_t streamOffset, std::span<const std::byte> bytes);
    [[nodiscard]] std::size_t makeArtifactRoom(std::size_t requestedBytes, CommandBlockId activeBlockId);
    void expireArtifact(CommandOutputArtifact &artifact) noexcept;
    void eraseArtifact(CommandBlockId id) noexcept;
    void recordGap(CommandBlock &block, std::uint64_t begin, std::uint64_t end);
    static void updateCoverage(CommandBlock &block) noexcept;

    CommandBlockStoreLimits m_limits;
    std::deque<CommandBlock> m_blocks;
    std::deque<CommandOutputArtifact> m_artifacts;
    std::size_t m_artifactBytes = 0;
    CommandBlockId m_nextId = 1;
};

} // namespace ztermy::terminal
