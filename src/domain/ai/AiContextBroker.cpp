#include "domain/ai/AiContextBroker.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace ztermy::ai
{
namespace
{

constexpr std::string_view truncationMarker = "\n...[truncated]...\n";
constexpr std::string_view lineTruncationMarker = "...[truncated]...\n";

[[nodiscard]] std::string blockId(const terminal::CommandBlock &block)
{
    return "command-block:" + block.sessionId + ':' + std::to_string(block.sessionGeneration) + ':'
           + std::to_string(block.id);
}

[[nodiscard]] std::string frameId(const AiTerminalFrameContext &frame)
{
    if (!frame.id.empty())
    {
        return "terminal-frame:" + frame.id;
    }
    return "terminal-frame:" + frame.sessionId + ':' + std::to_string(frame.sessionGeneration);
}

[[nodiscard]] bool isUtf8Continuation(const char value) noexcept
{
    return (static_cast<unsigned char>(value) & 0xC0U) == 0x80U;
}

[[nodiscard]] std::string normalizedTerminalText(const std::span<const std::byte> bytes)
{
    enum class State : std::uint8_t
    {
        normal,
        escape,
        csi,
        osc,
        oscEscape,
    };

    std::string output;
    output.reserve(bytes.size());
    auto state = State::normal;
    auto previousWasCarriageReturn = false;
    for (const auto byte : bytes)
    {
        const auto value = static_cast<unsigned char>(byte);
        switch (state)
        {
            case State::normal:
                if (value == 0x1BU)
                {
                    state = State::escape;
                }
                else if (value == '\r')
                {
                    if (output.empty() || output.back() != '\n')
                    {
                        output.push_back('\n');
                    }
                    previousWasCarriageReturn = true;
                }
                else if (value == '\n')
                {
                    if (!previousWasCarriageReturn)
                    {
                        output.push_back('\n');
                    }
                    previousWasCarriageReturn = false;
                }
                else if (value == '\t' || value >= 0x20U)
                {
                    output.push_back(static_cast<char>(value));
                    previousWasCarriageReturn = false;
                }
                break;
            case State::escape:
                if (value == '[')
                {
                    state = State::csi;
                }
                else if (value == ']')
                {
                    state = State::osc;
                }
                else
                {
                    state = State::normal;
                }
                break;
            case State::csi:
                if (value >= 0x40U && value <= 0x7EU)
                {
                    state = State::normal;
                }
                break;
            case State::osc:
                if (value == 0x07U)
                {
                    state = State::normal;
                }
                else if (value == 0x1BU)
                {
                    state = State::oscEscape;
                }
                break;
            case State::oscEscape:
                state = value == '\\' ? State::normal : State::osc;
                break;
        }
    }
    return output;
}

[[nodiscard]] std::string normalizedTerminalText(const std::string_view text)
{
    return normalizedTerminalText(std::as_bytes(std::span(text)));
}

[[nodiscard]] std::size_t lineCount(const std::string_view text)
{
    if (text.empty())
    {
        return 0;
    }
    const auto newlineCount = static_cast<std::size_t>(std::ranges::count(text, '\n'));
    return newlineCount + (text.back() == '\n' ? 0 : 1);
}

void capLines(std::string &text, const std::size_t maximumLines, bool &truncated)
{
    if (maximumLines == 0)
    {
        truncated = truncated || !text.empty();
        text.clear();
        return;
    }
    std::vector<std::size_t> starts{0};
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        if (text[index] == '\n' && index + 1 < text.size())
        {
            starts.push_back(index + 1);
        }
    }
    if (starts.size() <= maximumLines)
    {
        return;
    }
    const auto retainedLines = maximumLines - 1;
    const auto headLines = retainedLines / 2;
    const auto tailLines = retainedLines - headLines;
    const auto headEnd = starts[headLines];
    const auto tailStart = tailLines == 0 ? text.size() : starts[starts.size() - tailLines];
    text = text.substr(0, headEnd) + std::string(lineTruncationMarker) + text.substr(tailStart);
    truncated = true;
}

void capBytes(std::string &text, const std::size_t maximumBytes, bool &truncated)
{
    if (text.size() <= maximumBytes)
    {
        return;
    }
    if (maximumBytes <= truncationMarker.size())
    {
        auto boundary = maximumBytes;
        while (boundary > 0 && boundary < text.size() && isUtf8Continuation(text[boundary]))
        {
            --boundary;
        }
        text.resize(boundary);
        truncated = true;
        return;
    }

    const auto available = maximumBytes - truncationMarker.size();
    auto headBytes = available / 2;
    auto tailStart = text.size() - (available - headBytes);
    while (headBytes > 0 && headBytes < text.size() && isUtf8Continuation(text[headBytes]))
    {
        --headBytes;
    }
    while (tailStart < text.size() && isUtf8Continuation(text[tailStart]))
    {
        ++tailStart;
    }
    text = text.substr(0, headBytes) + std::string(truncationMarker) + text.substr(tailStart);
    truncated = true;
}

[[nodiscard]] std::size_t metadataBytes(const AiContextItem &item)
{
    return std::size_t{64} + item.id.size() + item.title.size() + item.command.size() + item.workingDirectory.size()
           + item.sessionId.size() + item.host.size() + item.shell.size();
}

void redactField(std::string &field, AiContextItem &item, const AiContextRedactor &redactor,
                 const std::span<const AiUserRedactionRule> rules, std::unordered_set<std::string> &invalidRuleIds)
{
    const auto result = redactor.redact(field, rules);
    field = result.text;
    item.redactionCount += result.totalRedactions();
    invalidRuleIds.insert(result.invalidRuleIds.begin(), result.invalidRuleIds.end());
}

void finalizeItem(AiContextItem &item, const AiContextLimits &limits, const AiContextRedactor &redactor,
                  const std::span<const AiUserRedactionRule> rules, std::unordered_set<std::string> &invalidRuleIds)
{
    item.id = normalizedTerminalText(item.id);
    item.title = normalizedTerminalText(item.title);
    item.command = normalizedTerminalText(item.command);
    item.content = normalizedTerminalText(item.content);
    item.workingDirectory = normalizedTerminalText(item.workingDirectory);
    item.sessionId = normalizedTerminalText(item.sessionId);
    item.host = normalizedTerminalText(item.host);
    item.shell = normalizedTerminalText(item.shell);
    capBytes(item.content, limits.maxRedactionWindowBytes, item.truncated);
    redactField(item.title, item, redactor, rules, invalidRuleIds);
    redactField(item.command, item, redactor, rules, invalidRuleIds);
    redactField(item.content, item, redactor, rules, invalidRuleIds);
    redactField(item.workingDirectory, item, redactor, rules, invalidRuleIds);
    redactField(item.host, item, redactor, rules, invalidRuleIds);
    redactField(item.shell, item, redactor, rules, invalidRuleIds);
    item.redacted = item.redactionCount > 0;
    const auto metadata = metadataBytes(item);
    const auto maximumContentBytes = metadata >= limits.maxItemBytes ? std::size_t{0} : limits.maxItemBytes - metadata;
    capLines(item.content, limits.maxItemLines, item.truncated);
    capBytes(item.content, maximumContentBytes, item.truncated);
    item.lineCount = lineCount(item.content) + (item.command.empty() ? 0 : 1);
    item.accountedBytes = metadataBytes(item) + item.content.size();
    item.estimatedTokens = (item.accountedBytes + 3) / 4;
}

[[nodiscard]] AiContextItem commandItem(const terminal::CommandBlock &block, const bool automatic, const bool pinned,
                                        const AiContextLimits &limits, const AiContextRedactor &redactor,
                                        const std::span<const AiUserRedactionRule> rules,
                                        std::unordered_set<std::string> &invalidRuleIds)
{
    AiContextItem item{.id = blockId(block),
                       .kind = AiContextItemKind::commandBlock,
                       .title =
                           block.exitStatus.has_value() && block.exitStatus.value() != 0 ? "Failed command" : "Command",
                       .content = normalizedTerminalText(block.retainedOutput),
                       .command = block.command,
                       .workingDirectory = block.workingDirectory,
                       .sessionId = block.sessionId,
                       .host = block.host,
                       .shell = block.shell,
                       .sessionGeneration = block.sessionGeneration,
                       .capability = block.capability,
                       .boundaryConfidence = block.boundaryConfidence,
                       .outputCoverage = block.outputCoverage,
                       .exitStatus = block.exitStatus,
                       .pinned = pinned,
                       .automatic = automatic,
                       .truncated = !block.hasCompleteOutput()};
    finalizeItem(item, limits, redactor, rules, invalidRuleIds);
    return item;
}

[[nodiscard]] const terminal::CommandBlock *primaryBlock(const std::span<const terminal::CommandBlock> blocks,
                                                         const AiContextRequest &request)
{
    if (request.primaryBlockId.has_value())
    {
        const auto block = std::ranges::find(blocks, request.primaryBlockId.value(), &terminal::CommandBlock::id);
        return block == blocks.end() ? nullptr : &*block;
    }
    const auto iterator = std::ranges::find_if(blocks.rbegin(), blocks.rend(), [&request](const auto &block) {
        if (block.state != terminal::CommandBlockState::finished)
        {
            return false;
        }
        return !request.preferLastFailure || (block.exitStatus.has_value() && block.exitStatus.value() != 0);
    });
    return iterator == blocks.rend() ? nullptr : &*iterator;
}

[[nodiscard]] bool sameSession(const terminal::CommandBlock &left, const terminal::CommandBlock &right)
{
    return left.sessionId == right.sessionId && left.sessionGeneration == right.sessionGeneration;
}

} // namespace

std::string AiContextBroker::normalizeTerminalText(const std::string_view text)
{
    return normalizedTerminalText(text);
}

AiContextBroker::AiContextBroker(const AiContextLimits limits) : m_limits(limits) {}

const AiContextLimits &AiContextBroker::limits() const noexcept
{
    return m_limits;
}

AiContextBundle AiContextBroker::build(const terminal::CommandBlockStore &store, const AiContextRequest &request) const
{
    const auto &storedBlocks = store.blocks();
    const std::vector<terminal::CommandBlock> blocks(storedBlocks.begin(), storedBlocks.end());
    return build(blocks, request);
}

AiContextBundle AiContextBroker::build(const std::span<const terminal::CommandBlock> blocks,
                                       const AiContextRequest &request) const
{
    std::vector<std::pair<int, AiContextItem>> candidates;
    AiContextRedactor redactor;
    std::unordered_set<std::string> invalidRuleIds;
    const auto validation = redactor.redact({}, request.redactionRules);
    invalidRuleIds.insert(validation.invalidRuleIds.begin(), validation.invalidRuleIds.end());
    for (const auto &explicitItem : request.explicitItems)
    {
        const auto id = "attachment:" + explicitItem.id;
        if (request.excludedItemIds.contains(id))
        {
            continue;
        }
        AiContextItem item{.id = id,
                           .kind = AiContextItemKind::explicitAttachment,
                           .title = explicitItem.title,
                           .content = explicitItem.content,
                           .host = explicitItem.source,
                           .pinned = request.pinnedItemIds.contains(id)};
        finalizeItem(item, m_limits, redactor, request.redactionRules, invalidRuleIds);
        candidates.emplace_back(0, std::move(item));
    }

    const auto *primary = primaryBlock(blocks, request);
    if (primary != nullptr)
    {
        const auto id = blockId(*primary);
        if (!request.excludedItemIds.contains(id))
        {
            const auto pinned = request.pinnedItemIds.contains(id);
            candidates.emplace_back(pinned ? 5 : 10, commandItem(*primary, false, pinned, m_limits, redactor,
                                                                 request.redactionRules, invalidRuleIds));
        }
    }

    if (request.currentFrame.has_value())
    {
        const auto &frame = request.currentFrame.value();
        const auto id = frameId(frame);
        if (!request.excludedItemIds.contains(id))
        {
            const auto pinned = request.pinnedItemIds.contains(id);
            AiContextItem item{.id = id,
                               .kind = AiContextItemKind::currentTerminalFrame,
                               .title = "Current terminal frame",
                               .content = frame.content,
                               .workingDirectory = frame.workingDirectory,
                               .sessionId = frame.sessionId,
                               .host = frame.host,
                               .shell = frame.shell,
                               .sessionGeneration = frame.sessionGeneration,
                               .capability = frame.capability,
                               .pinned = pinned};
            finalizeItem(item, m_limits, redactor, request.redactionRules, invalidRuleIds);
            candidates.emplace_back(pinned ? 5 : 20, std::move(item));
        }
    }

    if (request.automaticContextEnabled && primary != nullptr)
    {
        std::size_t included = 0;
        const auto primaryPosition = std::ranges::find_if(blocks, [primary](const auto &block) {
            return block.id == primary->id;
        });
        for (auto iterator = std::make_reverse_iterator(primaryPosition);
             iterator != blocks.rend() && included < m_limits.maxPrecedingBlocks; ++iterator)
        {
            if (iterator->state != terminal::CommandBlockState::finished || !sameSession(*iterator, *primary))
            {
                continue;
            }
            const auto id = blockId(*iterator);
            if (request.excludedItemIds.contains(id))
            {
                continue;
            }
            const auto pinned = request.pinnedItemIds.contains(id);
            candidates.emplace_back(
                pinned ? 5 : 50 + static_cast<int>(included),
                commandItem(*iterator, true, pinned, m_limits, redactor, request.redactionRules, invalidRuleIds));
            ++included;
        }
    }

    std::ranges::stable_sort(candidates, {}, &std::pair<int, AiContextItem>::first);
    AiContextBundle bundle;
    for (auto &[priority, item] : candidates)
    {
        static_cast<void>(priority);
        const auto wouldExceed = bundle.totalBytes + item.accountedBytes > m_limits.maxTotalBytes
                                 || bundle.totalLines + item.lineCount > m_limits.maxTotalLines
                                 || bundle.estimatedTokens + item.estimatedTokens > m_limits.maxEstimatedTokens;
        if (wouldExceed)
        {
            ++bundle.droppedItems;
            bundle.aggregateTruncated = true;
            continue;
        }
        bundle.totalBytes += item.accountedBytes;
        bundle.totalLines += item.lineCount;
        bundle.estimatedTokens += item.estimatedTokens;
        bundle.totalRedactions += item.redactionCount;
        bundle.items.push_back(std::move(item));
    }
    bundle.invalidRedactionRuleIds.assign(invalidRuleIds.begin(), invalidRuleIds.end());
    std::ranges::sort(bundle.invalidRedactionRuleIds);
    return bundle;
}

} // namespace ztermy::ai
