#include "domain/ai/AiReadTools.h"

#include <algorithm>

namespace ztermy::ai
{
namespace
{

[[nodiscard]] AiSessionSummary summarize(const AiTerminalReadSnapshot &session)
{
    return AiSessionSummary{.title = session.title,
                            .host = session.host,
                            .shell = session.shell,
                            .workingDirectory = session.workingDirectory,
                            .capability = session.capability,
                            .connected = session.connected,
                            .commandBlockCount = session.commandBlocks.size()};
}

[[nodiscard]] std::vector<std::string_view> linesOf(const std::string_view text)
{
    std::vector<std::string_view> lines;
    if (text.empty())
    {
        return lines;
    }
    std::size_t start = 0;
    while (start < text.size())
    {
        const auto newline = text.find('\n', start);
        if (newline == std::string_view::npos)
        {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, (newline - start) + 1));
        start = newline + 1;
    }
    return lines;
}

[[nodiscard]] std::size_t utf8Prefix(const std::string_view text, const std::size_t maximumBytes)
{
    auto count = std::min(text.size(), maximumBytes);
    while (count > 0 && count < text.size() && (static_cast<unsigned char>(text[count]) & 0xC0U) == 0x80U)
    {
        --count;
    }
    return count;
}

[[nodiscard]] std::string retainedOutput(const terminal::CommandBlock &block)
{
    if (block.retainedOutput.empty())
    {
        return {};
    }
    return {reinterpret_cast<const char *>(block.retainedOutput.data()), block.retainedOutput.size()};
}

[[nodiscard]] std::string bytesText(const std::span<const std::byte> bytes)
{
    if (bytes.empty())
    {
        return {};
    }
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

} // namespace

AiReadTools::AiReadTools(AiReadToolLimits limits) : m_limits(limits)
{
    m_limits.maxTerminalLines = std::max<std::size_t>(1, m_limits.maxTerminalLines);
    m_limits.maxTerminalBytes = std::max<std::size_t>(1, m_limits.maxTerminalBytes);
    m_limits.maxCommandOutputBytes = std::max<std::size_t>(1, m_limits.maxCommandOutputBytes);
}

const AiReadToolLimits &AiReadTools::limits() const noexcept
{
    return m_limits;
}

AiSessionSummary AiReadTools::readSessionInfo(const AiTerminalReadSnapshot &session) const
{
    return summarize(session);
}

std::expected<AiTerminalRange, AiReadToolError> AiReadTools::readTerminal(const AiTerminalReadSnapshot &session,
                                                                          const std::size_t firstLine,
                                                                          const std::size_t lineCount) const
{
    if (lineCount == 0 || lineCount > m_limits.maxTerminalLines)
    {
        return std::unexpected(
            AiReadToolError{.code = AiReadToolErrorCode::invalidArguments,
                            .message = "The requested terminal line count is outside the configured range."});
    }
    const auto lines = linesOf(session.terminalFrame);
    if (firstLine >= lines.size())
    {
        return std::unexpected(AiReadToolError{.code = AiReadToolErrorCode::rangeOutOfBounds,
                                               .message = "The terminal line range is out of bounds."});
    }

    const auto requestedEnd = firstLine + std::min(lineCount, lines.size() - firstLine);
    std::string content;
    content.reserve(std::min(m_limits.maxTerminalBytes, session.terminalFrame.size()));
    std::size_t emittedLines = 0;
    bool byteTruncated = false;
    for (auto index = firstLine; index < requestedEnd; ++index)
    {
        const auto remaining = m_limits.maxTerminalBytes - content.size();
        if (remaining == 0)
        {
            byteTruncated = true;
            break;
        }
        const auto count = utf8Prefix(lines[index], remaining);
        content.append(lines[index].substr(0, count));
        ++emittedLines;
        if (count != lines[index].size())
        {
            byteTruncated = true;
            break;
        }
    }

    const auto nextLine = firstLine + emittedLines;
    return AiTerminalRange{.content = std::move(content),
                           .firstLine = firstLine,
                           .lineCount = emittedLines,
                           .totalLines = lines.size(),
                           .nextLine = nextLine,
                           .hasMore = byteTruncated || nextLine < lines.size(),
                           .truncated = byteTruncated};
}

std::expected<AiCommandBlockRead, AiReadToolError>
AiReadTools::readCommandBlock(const AiTerminalReadSnapshot &session, const terminal::CommandBlockId blockId) const
{
    if (blockId == 0)
    {
        return std::unexpected(AiReadToolError{.code = AiReadToolErrorCode::invalidArguments,
                                               .message = "Command block id must be non-zero."});
    }
    const auto block = std::ranges::find(session.commandBlocks, blockId, &terminal::CommandBlock::id);
    if (block == session.commandBlocks.end())
    {
        return std::unexpected(AiReadToolError{.code = AiReadToolErrorCode::commandBlockNotFound,
                                               .message = "The command block does not exist in the target session."});
    }

    auto output = retainedOutput(*block);
    const auto outputBytes = utf8Prefix(output, m_limits.maxCommandOutputBytes);
    const bool truncated = outputBytes != output.size();
    output.resize(outputBytes);
    return AiCommandBlockRead{.id = block->id,
                              .command = block->command,
                              .workingDirectory = block->workingDirectory,
                              .host = block->host,
                              .shell = block->shell,
                              .capability = block->capability,
                              .boundaryConfidence = block->boundaryConfidence,
                              .outputCoverage = block->outputCoverage,
                              .state = block->state,
                              .exitStatus = block->exitStatus,
                              .output = std::move(output),
                              .observedOutputBytes = block->observedOutputBytes,
                              .omittedOutputBytes = block->omittedOutputBytes,
                              .missingOutputBytes = block->missingOutputBytes,
                              .truncated = truncated || block->omittedOutputBytes > 0,
                              .hasInterleavedOutput = block->hasInterleavedOutput};
}

std::expected<AiCommandOutputRead, AiReadToolError>
AiReadTools::readCommandOutput(const AiTerminalReadSnapshot &session, const terminal::CommandBlockId blockId,
                               const std::uint64_t afterCursor, const std::size_t maximumBytes) const
{
    if (blockId == 0 || maximumBytes == 0 || maximumBytes > m_limits.maxCommandOutputBytes)
    {
        return std::unexpected(
            AiReadToolError{.code = AiReadToolErrorCode::invalidArguments,
                            .message = "Command output reads require a block id and a bounded positive byte count."});
    }
    if (session.commandOutputReader)
    {
        auto artifact = session.commandOutputReader(blockId, afterCursor, maximumBytes);
        if (!artifact)
        {
            switch (artifact.error().code)
            {
                case terminal::CommandOutputArtifactErrorCode::blockNotFound:
                    return std::unexpected(
                        AiReadToolError{.code = AiReadToolErrorCode::commandBlockNotFound,
                                        .message = "The command block does not exist in the current terminal."});
                case terminal::CommandOutputArtifactErrorCode::cursorExpired:
                    return std::unexpected(
                        AiReadToolError{.code = AiReadToolErrorCode::cursorExpired,
                                        .message = "The command-output cursor precedes the retained artifact.",
                                        .nextAvailableCursor = artifact.error().nextAvailableCursor});
                case terminal::CommandOutputArtifactErrorCode::artifactExpired:
                    return std::unexpected(
                        AiReadToolError{.code = AiReadToolErrorCode::artifactExpired,
                                        .message = "The bounded command-output artifact has expired.",
                                        .nextAvailableCursor = artifact.error().nextAvailableCursor});
                case terminal::CommandOutputArtifactErrorCode::rangeOutOfBounds:
                    return std::unexpected(
                        AiReadToolError{.code = AiReadToolErrorCode::rangeOutOfBounds,
                                        .message = "The command-output cursor is beyond the observed stream."});
                case terminal::CommandOutputArtifactErrorCode::outputUnavailable:
                    return std::unexpected(AiReadToolError{
                        .code = AiReadToolErrorCode::outputUnavailable,
                        .message = "The remaining command output exceeds the retained artifact limit."});
                case terminal::CommandOutputArtifactErrorCode::limitExceeded:
                    return std::unexpected(AiReadToolError{
                        .code = AiReadToolErrorCode::invalidArguments,
                        .message = "The command-output byte limit cannot encode the next UTF-8 character."});
            }
            return std::unexpected(AiReadToolError{.code = AiReadToolErrorCode::outputUnavailable,
                                                   .message = "The command-output artifact is unavailable."});
        }
        return AiCommandOutputRead{
            .id = artifact->id,
            .state = artifact->state,
            .outputCoverage = artifact->outputCoverage,
            .exitStatus = artifact->exitStatus,
            .output = bytesText(artifact->output),
            .requestedCursor = artifact->requestedCursor,
            .nextCursor = artifact->nextCursor,
            .streamStart = artifact->streamStart,
            .streamEnd = artifact->streamEnd,
            .skippedBytes = artifact->skippedBytes,
            .artifactRetainedBytes = artifact->retainedBytes,
            .artifactOmittedBytes = artifact->omittedBytes,
            .hasMore = artifact->readableMore,
            .streamHasMore = artifact->streamHasMore,
            .truncated = artifact->pageTruncated,
            .artifactBacked = true,
            .artifactComplete = artifact->artifactComplete,
            .artifactExpired = artifact->artifactExpired,
        };
    }
    const auto block = std::ranges::find(session.commandBlocks, blockId, &terminal::CommandBlock::id);
    if (block == session.commandBlocks.end())
    {
        return std::unexpected(AiReadToolError{.code = AiReadToolErrorCode::commandBlockNotFound,
                                               .message = "The command block does not exist in the target session."});
    }

    const auto streamStart = block->firstOutputStreamOffset;
    const auto streamEnd = block->nextOutputStreamOffset;
    const auto cursor = afterCursor == 0 ? streamStart : afterCursor;
    if (cursor < streamStart)
    {
        return std::unexpected(AiReadToolError{.code = AiReadToolErrorCode::cursorExpired,
                                               .message = "The requested command-output cursor is no longer retained.",
                                               .nextAvailableCursor = streamStart});
    }
    if (cursor > streamEnd)
    {
        return std::unexpected(AiReadToolError{.code = AiReadToolErrorCode::rangeOutOfBounds,
                                               .message = "The command-output cursor is beyond the observed stream."});
    }

    const auto head = block->retainedHead();
    const auto tail = block->retainedTail();
    const auto headEnd = streamStart + head.size();
    const auto tailStart = tail.empty() ? headEnd : block->retainedTailStreamOffset;
    if (cursor > headEnd && cursor < tailStart)
    {
        return std::unexpected(AiReadToolError{.code = AiReadToolErrorCode::cursorExpired,
                                               .message = "The requested command-output cursor was evicted.",
                                               .nextAvailableCursor = tailStart});
    }

    std::span<const std::byte> source;
    std::uint64_t sourceStart = cursor;
    std::uint64_t skippedBytes = 0;
    if (cursor < headEnd)
    {
        source = head.subspan(static_cast<std::size_t>(cursor - streamStart));
    }
    else if (cursor == headEnd && !tail.empty() && tailStart > headEnd)
    {
        source = tail;
        sourceStart = tailStart;
        skippedBytes = tailStart - headEnd;
    }
    else if (!tail.empty() && cursor >= tailStart && cursor < streamEnd)
    {
        source = tail.subspan(static_cast<std::size_t>(cursor - tailStart));
    }

    const auto availableText = bytesText(source);
    const auto emittedBytes = utf8Prefix(availableText, maximumBytes);
    if (emittedBytes == 0 && !source.empty())
    {
        return std::unexpected(
            AiReadToolError{.code = AiReadToolErrorCode::invalidArguments,
                            .message = "The command-output byte limit is too small for the next UTF-8 character."});
    }
    auto output = availableText.substr(0, emittedBytes);
    const auto nextCursor = sourceStart + emittedBytes;
    const bool sourceHasMore = emittedBytes < source.size();
    const bool retainedGapAhead = cursor < headEnd && nextCursor == headEnd && tailStart > headEnd;
    const bool streamHasMore = nextCursor < streamEnd;
    return AiCommandOutputRead{.id = block->id,
                               .state = block->state,
                               .outputCoverage = block->outputCoverage,
                               .exitStatus = block->exitStatus,
                               .output = std::move(output),
                               .requestedCursor = afterCursor,
                               .nextCursor = nextCursor,
                               .streamStart = streamStart,
                               .streamEnd = streamEnd,
                               .skippedBytes = skippedBytes,
                               .artifactRetainedBytes = block->retainedOutput.size(),
                               .artifactOmittedBytes = block->omittedOutputBytes,
                               .hasMore = sourceHasMore || retainedGapAhead || streamHasMore,
                               .streamHasMore = streamHasMore,
                               .truncated = sourceHasMore,
                               .artifactComplete = block->hasCompleteOutput()};
}

} // namespace ztermy::ai
