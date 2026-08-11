#include "domain/ai/AiReadTools.h"

#include <algorithm>
#include <iterator>

namespace ztermy::ai
{
namespace
{

[[nodiscard]] AiSessionSummary summarize(const AiTerminalReadSnapshot &session)
{
    return AiSessionSummary{.sessionId = session.sessionId,
                            .title = session.title,
                            .host = session.host,
                            .shell = session.shell,
                            .workingDirectory = session.workingDirectory,
                            .sessionGeneration = session.sessionGeneration,
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

} // namespace

AiReadTools::AiReadTools(AiReadToolLimits limits) : m_limits(limits)
{
    m_limits.maxSessions = std::max<std::size_t>(1, m_limits.maxSessions);
    m_limits.maxTerminalLines = std::max<std::size_t>(1, m_limits.maxTerminalLines);
    m_limits.maxTerminalBytes = std::max<std::size_t>(1, m_limits.maxTerminalBytes);
    m_limits.maxCommandOutputBytes = std::max<std::size_t>(1, m_limits.maxCommandOutputBytes);
}

const AiReadToolLimits &AiReadTools::limits() const noexcept
{
    return m_limits;
}

std::expected<std::vector<AiSessionSummary>, AiReadToolError>
AiReadTools::listSessions(const std::span<const AiTerminalReadSnapshot> sessions) const
{
    if (sessions.size() > m_limits.maxSessions)
    {
        return std::unexpected(AiReadToolError{.code = AiReadToolErrorCode::limitExceeded,
                                               .message = "The session list exceeds the configured read-tool limit."});
    }
    std::vector<AiSessionSummary> summaries;
    summaries.reserve(sessions.size());
    std::ranges::transform(sessions, std::back_inserter(summaries), &summarize);
    return summaries;
}

std::expected<AiSessionSummary, AiReadToolError>
AiReadTools::readSessionInfo(const std::span<const AiTerminalReadSnapshot> sessions, const std::string_view sessionId,
                             const std::uint64_t sessionGeneration) const
{
    const auto session = findSession(sessions, sessionId, sessionGeneration);
    if (!session.has_value())
    {
        return std::unexpected(session.error());
    }
    return summarize(**session);
}

std::expected<AiTerminalRange, AiReadToolError>
AiReadTools::readTerminal(const std::span<const AiTerminalReadSnapshot> sessions, const std::string_view sessionId,
                          const std::uint64_t sessionGeneration, const std::size_t firstLine,
                          const std::size_t lineCount) const
{
    if (lineCount == 0 || lineCount > m_limits.maxTerminalLines)
    {
        return std::unexpected(
            AiReadToolError{.code = AiReadToolErrorCode::invalidArguments,
                            .message = "The requested terminal line count is outside the configured range."});
    }
    const auto session = findSession(sessions, sessionId, sessionGeneration);
    if (!session.has_value())
    {
        return std::unexpected(session.error());
    }

    const auto lines = linesOf((*session)->terminalFrame);
    if (firstLine >= lines.size())
    {
        return std::unexpected(AiReadToolError{.code = AiReadToolErrorCode::rangeOutOfBounds,
                                               .message = "The terminal line range is out of bounds."});
    }

    const auto requestedEnd = firstLine + std::min(lineCount, lines.size() - firstLine);
    std::string content;
    content.reserve(std::min(m_limits.maxTerminalBytes, (*session)->terminalFrame.size()));
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
    return AiTerminalRange{.sessionId = (*session)->sessionId,
                           .sessionGeneration = (*session)->sessionGeneration,
                           .content = std::move(content),
                           .firstLine = firstLine,
                           .lineCount = emittedLines,
                           .totalLines = lines.size(),
                           .nextLine = nextLine,
                           .hasMore = byteTruncated || nextLine < lines.size(),
                           .truncated = byteTruncated};
}

std::expected<AiCommandBlockRead, AiReadToolError>
AiReadTools::readCommandBlock(const std::span<const AiTerminalReadSnapshot> sessions, const std::string_view sessionId,
                              const std::uint64_t sessionGeneration, const terminal::CommandBlockId blockId) const
{
    if (blockId == 0)
    {
        return std::unexpected(AiReadToolError{.code = AiReadToolErrorCode::invalidArguments,
                                               .message = "Command block id must be non-zero."});
    }
    const auto session = findSession(sessions, sessionId, sessionGeneration);
    if (!session.has_value())
    {
        return std::unexpected(session.error());
    }
    const auto block = std::ranges::find((*session)->commandBlocks, blockId, &terminal::CommandBlock::id);
    if (block == (*session)->commandBlocks.end())
    {
        return std::unexpected(AiReadToolError{.code = AiReadToolErrorCode::commandBlockNotFound,
                                               .message = "The command block does not exist in the target session."});
    }

    auto output = retainedOutput(*block);
    const auto outputBytes = utf8Prefix(output, m_limits.maxCommandOutputBytes);
    const bool truncated = outputBytes != output.size();
    output.resize(outputBytes);
    return AiCommandBlockRead{.id = block->id,
                              .sessionId = block->sessionId,
                              .sessionGeneration = block->sessionGeneration,
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

std::expected<const AiTerminalReadSnapshot *, AiReadToolError>
AiReadTools::findSession(const std::span<const AiTerminalReadSnapshot> sessions, const std::string_view sessionId,
                         const std::uint64_t sessionGeneration) const
{
    if (sessionId.empty())
    {
        return std::unexpected(
            AiReadToolError{.code = AiReadToolErrorCode::invalidArguments, .message = "Session id must not be empty."});
    }
    const auto session = std::ranges::find(sessions, sessionId, &AiTerminalReadSnapshot::sessionId);
    if (session == sessions.end())
    {
        return std::unexpected(AiReadToolError{.code = AiReadToolErrorCode::sessionNotFound,
                                               .message = "The target terminal session no longer exists."});
    }
    if (session->sessionGeneration != sessionGeneration)
    {
        return std::unexpected(
            AiReadToolError{.code = AiReadToolErrorCode::staleSessionGeneration,
                            .message = "The terminal session generation changed; refresh the target before reading."});
    }
    return &*session;
}

} // namespace ztermy::ai
