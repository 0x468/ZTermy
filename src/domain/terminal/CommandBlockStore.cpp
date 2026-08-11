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
                                                                      const std::int64_t finishedUtcMs)
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
    return {};
}

void CommandBlockStore::clear() noexcept
{
    m_blocks.clear();
}

CommandBlock *CommandBlockStore::findMutable(const CommandBlockId id) noexcept
{
    const auto found = std::ranges::find(m_blocks, id, &CommandBlock::id);
    return found == m_blocks.end() ? nullptr : &*found;
}

bool CommandBlockStore::evictOldestFinished()
{
    const auto found = std::ranges::find(m_blocks, CommandBlockState::finished, &CommandBlock::state);
    if (found == m_blocks.end())
    {
        return false;
    }
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
    if (block.missingOutputBytes > 0)
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
