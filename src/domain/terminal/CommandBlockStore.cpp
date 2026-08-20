#include "domain/terminal/CommandBlockStore.h"

#include <algorithm>
#include <limits>

namespace ztermy::terminal
{

namespace
{

[[nodiscard]] std::uint64_t saturatedAdd(const std::uint64_t left, const std::uint64_t right) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
    {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left + right;
}

void appendBytes(std::vector<std::byte> &destination, const std::span<const std::byte> source)
{
    destination.insert(destination.end(), source.begin(), source.end());
}

[[nodiscard]] std::size_t utf8Prefix(const std::span<const std::byte> bytes, const std::size_t maximumBytes) noexcept
{
    auto count = std::min(bytes.size(), maximumBytes);
    while (count > 0 && count < bytes.size() && (std::to_integer<unsigned char>(bytes[count]) & 0xC0U) == 0x80U)
    {
        --count;
    }
    return count;
}

} // namespace

std::span<const std::byte> CommandBlock::retainedHead() const noexcept
{
    return std::span(retainedOutput).first(std::min(retainedHeadBytes, retainedOutput.size()));
}

std::span<const std::byte> CommandBlock::retainedTail() const noexcept
{
    return std::span(retainedOutput).subspan(std::min(retainedHeadBytes, retainedOutput.size()));
}

bool CommandBlock::hasCompleteOutput() const noexcept
{
    return outputCoverage == CommandOutputCoverage::complete;
}

CommandBlockStore::CommandBlockStore(CommandBlockStoreLimits limits) : m_limits(limits)
{
    m_limits.maxBlocks = std::max<std::size_t>(1, m_limits.maxBlocks);
    m_limits.maxOutputBytesPerBlock = std::max<std::size_t>(1, m_limits.maxOutputBytesPerBlock);
    m_limits.retainedHeadBytes = std::min(m_limits.retainedHeadBytes, m_limits.maxOutputBytesPerBlock);
    m_limits.maxGapRangesPerBlock = std::max<std::size_t>(1, m_limits.maxGapRangesPerBlock);
    m_limits.maxArtifactBytesPerBlock = std::max<std::size_t>(1, m_limits.maxArtifactBytesPerBlock);
    m_limits.maxArtifactBytesTotal = std::max<std::size_t>(1, m_limits.maxArtifactBytesTotal);
    m_limits.maxArtifactSegmentsPerBlock = std::max<std::size_t>(1, m_limits.maxArtifactSegmentsPerBlock);
}

const CommandBlockStoreLimits &CommandBlockStore::limits() const noexcept
{
    return m_limits;
}

const std::deque<CommandBlock> &CommandBlockStore::blocks() const noexcept
{
    return m_blocks;
}

const CommandBlock *CommandBlockStore::find(const CommandBlockId id) const noexcept
{
    const auto found = std::ranges::find(m_blocks, id, &CommandBlock::id);
    return found == m_blocks.end() ? nullptr : &*found;
}

std::expected<CommandBlockId, CommandBlockStoreError> CommandBlockStore::begin(CommandBlockStart start)
{
    while (m_blocks.size() >= m_limits.maxBlocks)
    {
        if (!evictOldestFinished())
        {
            return std::unexpected(CommandBlockStoreError::capacityExceeded);
        }
    }

    const CommandBlockId id = m_nextId++;
    m_blocks.push_back(CommandBlock{
        .id = id,
        .command = std::move(start.command),
        .workingDirectory = std::move(start.workingDirectory),
        .sessionId = std::move(start.sessionId),
        .host = std::move(start.host),
        .shell = std::move(start.shell),
        .sessionGeneration = start.sessionGeneration,
        .capability = start.capability,
        .commandProvenance = start.commandProvenance,
        .boundaryConfidence = start.boundaryConfidence,
        .startedUtcMs = start.startedUtcMs,
        .firstOutputStreamOffset = start.outputStreamOffset,
        .nextOutputStreamOffset = start.outputStreamOffset,
        .retainedTailStreamOffset = start.outputStreamOffset,
    });
    m_artifacts.push_back(CommandOutputArtifact{
        .blockId = id,
        .streamStart = start.outputStreamOffset,
        .streamEnd = start.outputStreamOffset,
    });
    return id;
}

std::expected<void, CommandBlockStoreError> CommandBlockStore::append(const CommandBlockId id,
                                                                      CommandOutputObservation observation)
{
    CommandBlock *block = findMutable(id);
    if (block == nullptr)
    {
        return std::unexpected(CommandBlockStoreError::blockNotFound);
    }
    if (block->state == CommandBlockState::finished)
    {
        return std::unexpected(CommandBlockStoreError::blockAlreadyFinished);
    }

    const std::uint64_t observationSize = observation.bytes.size();
    const std::uint64_t observationEnd = saturatedAdd(observation.streamOffset, observationSize);
    if (observationEnd <= block->nextOutputStreamOffset)
    {
        block->hasInterleavedOutput = block->hasInterleavedOutput || observation.interleaved;
        updateCoverage(*block);
        return {};
    }

    std::size_t overlap = 0;
    if (observation.streamOffset < block->nextOutputStreamOffset)
    {
        overlap = static_cast<std::size_t>(block->nextOutputStreamOffset - observation.streamOffset);
    }
    else if (observation.streamOffset > block->nextOutputStreamOffset)
    {
        recordGap(*block, block->nextOutputStreamOffset, observation.streamOffset);
        block->missingOutputBytes =
            saturatedAdd(block->missingOutputBytes, observation.streamOffset - block->nextOutputStreamOffset);
    }

    const auto newBytes = observation.bytes.subspan(overlap);
    if (CommandOutputArtifact *artifact = findArtifactMutable(id))
    {
        retainArtifact(*artifact, saturatedAdd(observation.streamOffset, overlap), newBytes);
        artifact->streamEnd = observationEnd;
    }
    block->observedOutputBytes = saturatedAdd(block->observedOutputBytes, newBytes.size());
    block->nextOutputStreamOffset = observationEnd;
    block->hasInterleavedOutput = block->hasInterleavedOutput || observation.interleaved;
    retain(*block, newBytes);
    block->omittedOutputBytes = block->observedOutputBytes - block->retainedOutput.size();
    updateCoverage(*block);
    return {};
}

std::expected<void, CommandBlockStoreError> CommandBlockStore::finish(const CommandBlockId id,
                                                                      const std::optional<int> exitStatus,
                                                                      const std::int64_t finishedUtcMs,
                                                                      const CommandCompletionReason reason)
{
    CommandBlock *block = findMutable(id);
    if (block == nullptr)
    {
        return std::unexpected(CommandBlockStoreError::blockNotFound);
    }
    if (block->state == CommandBlockState::finished)
    {
        return std::unexpected(CommandBlockStoreError::blockAlreadyFinished);
    }

    block->state = CommandBlockState::finished;
    block->exitStatus = exitStatus;
    block->finishedUtcMs = finishedUtcMs;
    block->completionReason = reason;
    if (CommandOutputArtifact *artifact = findArtifactMutable(id))
    {
        artifact->finished = true;
    }
    return {};
}

std::expected<void, CommandBlockStoreError> CommandBlockStore::markOutputUnknown(const CommandBlockId id)
{
    CommandBlock *block = findMutable(id);
    if (block == nullptr)
    {
        return std::unexpected(CommandBlockStoreError::blockNotFound);
    }
    block->outputCoverageUncertain = true;
    updateCoverage(*block);
    return {};
}

std::expected<CommandOutputArtifactPage, CommandOutputArtifactError>
CommandBlockStore::readOutputArtifact(const CommandBlockId id, const std::uint64_t afterCursor,
                                      const std::size_t maximumBytes) const
{
    const CommandBlock *block = find(id);
    const CommandOutputArtifact *artifact = findArtifact(id);
    if (block == nullptr || artifact == nullptr)
    {
        return std::unexpected(CommandOutputArtifactError{.code = CommandOutputArtifactErrorCode::blockNotFound});
    }
    if (maximumBytes == 0)
    {
        return std::unexpected(CommandOutputArtifactError{.code = CommandOutputArtifactErrorCode::limitExceeded});
    }

    const std::uint64_t cursor = afterCursor == 0 ? artifact->streamStart : afterCursor;
    if (cursor < artifact->streamStart)
    {
        return std::unexpected(CommandOutputArtifactError{
            .code = CommandOutputArtifactErrorCode::cursorExpired,
            .nextAvailableCursor = artifact->streamStart,
        });
    }
    if (cursor > artifact->streamEnd)
    {
        return std::unexpected(CommandOutputArtifactError{.code = CommandOutputArtifactErrorCode::rangeOutOfBounds});
    }

    std::uint64_t retainedBytes = 0;
    for (const auto &segment : artifact->segments)
    {
        retainedBytes = saturatedAdd(retainedBytes, segment.bytes.size());
    }
    const bool artifactComplete = !artifact->expired && artifact->omittedBytes == 0 && block->missingOutputBytes == 0;
    const CommandOutputCoverage artifactCoverage =
        artifactComplete && block->outputCoverage == CommandOutputCoverage::boundedHeadTail
            ? CommandOutputCoverage::complete
            : block->outputCoverage;
    CommandOutputArtifactPage page{
        .id = id,
        .state = block->state,
        .outputCoverage = artifactCoverage,
        .exitStatus = block->exitStatus,
        .requestedCursor = afterCursor,
        .nextCursor = cursor,
        .streamStart = artifact->streamStart,
        .streamEnd = artifact->streamEnd,
        .retainedBytes = retainedBytes,
        .omittedBytes = artifact->omittedBytes,
        .streamHasMore = cursor < artifact->streamEnd,
        .artifactComplete = artifactComplete,
        .artifactExpired = artifact->expired,
    };
    if (cursor == artifact->streamEnd)
    {
        page.streamHasMore = false;
        return page;
    }

    const CommandOutputArtifactSegment *source = nullptr;
    for (const auto &segment : artifact->segments)
    {
        const auto segmentEnd = saturatedAdd(segment.streamOffset, segment.bytes.size());
        if (cursor < segmentEnd)
        {
            source = &segment;
            break;
        }
    }
    if (source == nullptr)
    {
        return std::unexpected(CommandOutputArtifactError{
            .code = artifact->expired ? CommandOutputArtifactErrorCode::artifactExpired
                                      : CommandOutputArtifactErrorCode::outputUnavailable,
        });
    }

    const std::uint64_t sourceCursor = std::max(cursor, source->streamOffset);
    page.skippedBytes = sourceCursor - cursor;
    const auto sourceOffset = static_cast<std::size_t>(sourceCursor - source->streamOffset);
    const auto available = std::span(source->bytes).subspan(sourceOffset);
    const std::size_t emittedBytes = utf8Prefix(available, maximumBytes);
    if (emittedBytes == 0 && !available.empty())
    {
        return std::unexpected(CommandOutputArtifactError{.code = CommandOutputArtifactErrorCode::limitExceeded});
    }
    page.output.assign(available.begin(), available.begin() + static_cast<std::ptrdiff_t>(emittedBytes));
    page.nextCursor = saturatedAdd(sourceCursor, emittedBytes);
    page.pageTruncated = emittedBytes < available.size();

    const auto sourcePosition = std::ranges::find_if(artifact->segments, [source](const auto &segment) {
        return &segment == source;
    });
    page.readableMore = page.pageTruncated || std::next(sourcePosition) != artifact->segments.end();
    page.streamHasMore = page.nextCursor < artifact->streamEnd;
    return page;
}

void CommandBlockStore::clear() noexcept
{
    m_blocks.clear();
    m_artifacts.clear();
    m_artifactBytes = 0;
}

CommandBlock *CommandBlockStore::findMutable(const CommandBlockId id) noexcept
{
    const auto found = std::ranges::find(m_blocks, id, &CommandBlock::id);
    return found == m_blocks.end() ? nullptr : &*found;
}

CommandBlockStore::CommandOutputArtifact *CommandBlockStore::findArtifactMutable(const CommandBlockId id) noexcept
{
    const auto found = std::ranges::find(m_artifacts, id, &CommandOutputArtifact::blockId);
    return found == m_artifacts.end() ? nullptr : &*found;
}

const CommandBlockStore::CommandOutputArtifact *CommandBlockStore::findArtifact(const CommandBlockId id) const noexcept
{
    const auto found = std::ranges::find(m_artifacts, id, &CommandOutputArtifact::blockId);
    return found == m_artifacts.end() ? nullptr : &*found;
}

bool CommandBlockStore::evictOldestFinished()
{
    const auto found = std::ranges::find(m_blocks, CommandBlockState::finished, &CommandBlock::state);
    if (found == m_blocks.end())
    {
        return false;
    }
    eraseArtifact(found->id);
    m_blocks.erase(found);
    return true;
}

void CommandBlockStore::retain(CommandBlock &block, const std::span<const std::byte> bytes)
{
    const std::size_t capacity = m_limits.maxOutputBytesPerBlock;
    const std::size_t headCapacity = m_limits.retainedHeadBytes;
    const std::size_t tailCapacity = capacity - headCapacity;

    if (block.omittedOutputBytes == 0 && block.retainedOutput.size() + bytes.size() <= capacity)
    {
        appendBytes(block.retainedOutput, bytes);
        block.retainedHeadBytes = block.retainedOutput.size();
        block.retainedTailStreamOffset = block.nextOutputStreamOffset;
        return;
    }

    if (block.omittedOutputBytes == 0)
    {
        std::vector<std::byte> bounded;
        bounded.reserve(capacity);

        const std::size_t headFromExisting = std::min(headCapacity, block.retainedOutput.size());
        appendBytes(bounded, std::span(block.retainedOutput).first(headFromExisting));
        if (headFromExisting < headCapacity)
        {
            appendBytes(bounded, bytes.first(std::min(headCapacity - headFromExisting, bytes.size())));
        }

        if (tailCapacity > 0)
        {
            if (bytes.size() >= tailCapacity)
            {
                appendBytes(bounded, bytes.last(tailCapacity));
            }
            else
            {
                const std::size_t fromExisting = std::min(tailCapacity - bytes.size(), block.retainedOutput.size());
                appendBytes(bounded, std::span(block.retainedOutput).last(fromExisting));
                appendBytes(bounded, bytes);
            }
        }

        block.retainedOutput = std::move(bounded);
        block.retainedHeadBytes = headCapacity;
        block.retainedTailStreamOffset = block.nextOutputStreamOffset - tailCapacity;
        return;
    }

    if (tailCapacity == 0)
    {
        return;
    }

    std::vector<std::byte> tail;
    tail.reserve(tailCapacity);
    const auto oldTail = block.retainedTail();
    if (bytes.size() >= tailCapacity)
    {
        appendBytes(tail, bytes.last(tailCapacity));
    }
    else
    {
        appendBytes(tail, oldTail.last(std::min(tailCapacity - bytes.size(), oldTail.size())));
        appendBytes(tail, bytes);
    }

    block.retainedOutput.resize(block.retainedHeadBytes);
    appendBytes(block.retainedOutput, tail);
    block.retainedTailStreamOffset = block.nextOutputStreamOffset - tail.size();
}

void CommandBlockStore::retainArtifact(CommandOutputArtifact &artifact, const std::uint64_t streamOffset,
                                       const std::span<const std::byte> bytes)
{
    artifact.observedBytes = saturatedAdd(artifact.observedBytes, bytes.size());
    if (bytes.empty())
    {
        return;
    }

    std::size_t retainedForArtifact = 0;
    for (const auto &segment : artifact.segments)
    {
        retainedForArtifact += segment.bytes.size();
    }
    const std::size_t artifactRoom =
        m_limits.maxArtifactBytesPerBlock - std::min(retainedForArtifact, m_limits.maxArtifactBytesPerBlock);
    std::size_t retainCount = std::min(bytes.size(), artifactRoom);
    const bool mergesLast =
        !artifact.segments.empty()
        && saturatedAdd(artifact.segments.back().streamOffset, artifact.segments.back().bytes.size()) == streamOffset;
    if (!mergesLast && artifact.segments.size() >= m_limits.maxArtifactSegmentsPerBlock)
    {
        retainCount = 0;
    }
    retainCount = makeArtifactRoom(retainCount, artifact.blockId);
    if (retainCount > 0)
    {
        const auto retained = bytes.first(retainCount);
        if (mergesLast)
        {
            appendBytes(artifact.segments.back().bytes, retained);
        }
        else
        {
            CommandOutputArtifactSegment segment{.streamOffset = streamOffset};
            appendBytes(segment.bytes, retained);
            artifact.segments.push_back(std::move(segment));
        }
        m_artifactBytes += retainCount;
    }
    artifact.omittedBytes = saturatedAdd(artifact.omittedBytes, bytes.size() - retainCount);
}

std::size_t CommandBlockStore::makeArtifactRoom(const std::size_t requestedBytes, const CommandBlockId activeBlockId)
{
    const std::size_t boundedRequest = std::min(requestedBytes, m_limits.maxArtifactBytesTotal);
    while (boundedRequest > m_limits.maxArtifactBytesTotal - std::min(m_artifactBytes, m_limits.maxArtifactBytesTotal))
    {
        const auto removable = std::ranges::find_if(m_artifacts, [activeBlockId](const CommandOutputArtifact &value) {
            return value.blockId != activeBlockId && value.finished && !value.segments.empty();
        });
        if (removable == m_artifacts.end())
        {
            break;
        }
        expireArtifact(*removable);
    }
    const std::size_t available =
        m_limits.maxArtifactBytesTotal - std::min(m_artifactBytes, m_limits.maxArtifactBytesTotal);
    return std::min(boundedRequest, available);
}

void CommandBlockStore::expireArtifact(CommandOutputArtifact &artifact) noexcept
{
    std::size_t released = 0;
    for (const auto &segment : artifact.segments)
    {
        released += segment.bytes.size();
    }
    m_artifactBytes -= std::min(m_artifactBytes, released);
    decltype(artifact.segments){}.swap(artifact.segments);
    artifact.omittedBytes = artifact.observedBytes;
    artifact.expired = true;
}

void CommandBlockStore::eraseArtifact(const CommandBlockId id) noexcept
{
    const auto found = std::ranges::find(m_artifacts, id, &CommandOutputArtifact::blockId);
    if (found == m_artifacts.end())
    {
        return;
    }
    std::size_t released = 0;
    for (const auto &segment : found->segments)
    {
        released += segment.bytes.size();
    }
    m_artifactBytes -= std::min(m_artifactBytes, released);
    m_artifacts.erase(found);
}

void CommandBlockStore::recordGap(CommandBlock &block, const std::uint64_t begin, const std::uint64_t end)
{
    if (begin >= end)
    {
        return;
    }
    if (!block.outputGaps.empty() && block.outputGaps.back().endStreamOffset >= begin)
    {
        block.outputGaps.back().endStreamOffset = std::max(block.outputGaps.back().endStreamOffset, end);
        return;
    }
    if (block.outputGaps.size() < m_limits.maxGapRangesPerBlock)
    {
        block.outputGaps.push_back({.beginStreamOffset = begin, .endStreamOffset = end});
        return;
    }

    // Keep metadata bounded. Coalescing overflow into the final range is
    // conservative: consumers may treat too much output as uncertain, but can
    // never mistake an unobserved interval for complete.
    block.outputGaps.back().endStreamOffset = end;
}

void CommandBlockStore::updateCoverage(CommandBlock &block) noexcept
{
    if (block.outputCoverageUncertain)
    {
        block.outputCoverage = CommandOutputCoverage::unknown;
    }
    else if (block.missingOutputBytes > 0)
    {
        block.outputCoverage = CommandOutputCoverage::gapped;
    }
    else if (block.hasInterleavedOutput)
    {
        block.outputCoverage = CommandOutputCoverage::interleaved;
    }
    else if (block.omittedOutputBytes > 0)
    {
        block.outputCoverage = CommandOutputCoverage::boundedHeadTail;
    }
    else
    {
        block.outputCoverage = CommandOutputCoverage::complete;
    }
}

} // namespace ztermy::terminal
