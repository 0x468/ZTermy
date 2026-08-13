#include "domain/ai/AiContextCompactor.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <numeric>
#include <string>

namespace ztermy::ai
{
namespace
{

[[nodiscard]] std::size_t utf8Bytes(const std::string_view value) noexcept
{
    return value.size();
}

[[nodiscard]] bool isUtf8Continuation(const char value) noexcept
{
    return (static_cast<unsigned char>(value) & 0xC0U) == 0x80U;
}

// Truncate at a UTF-8 boundary: never cut in the middle of a code point.
[[nodiscard]] std::string truncatedPrefix(const std::string_view value, const std::size_t maximumBytes)
{
    if (value.size() <= maximumBytes)
    {
        return std::string(value);
    }
    auto count = maximumBytes;
    while (count > 0 && count < value.size() && isUtf8Continuation(value[count]))
    {
        --count;
    }
    return std::string(value.substr(0, count));
}

[[nodiscard]] std::string truncatedSuffix(const std::string_view value, const std::size_t maximumBytes)
{
    if (value.size() <= maximumBytes)
    {
        return std::string(value);
    }
    auto start = value.size() - maximumBytes;
    while (start < value.size() && isUtf8Continuation(value[start]))
    {
        ++start;
    }
    return std::string(value.substr(start));
}

[[nodiscard]] std::size_t estimateTextTokens(const std::string_view text) noexcept
{
    // Provider-neutral heuristic shared with the context broker: one token
    // per four UTF-8 bytes on average.
    return (text.size() + 3) / 4;
}

[[nodiscard]] QString utf8(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

} // namespace

std::size_t AiContextCompactor::estimateTokens(const std::string_view text)
{
    return estimateTextTokens(text);
}

std::size_t AiContextCompactor::estimateRequestTokens(const AiGenerationRequest &request)
{
    std::size_t total = 0;
    total += estimateTextTokens(request.instructions);
    for (const auto &message : request.messages)
    {
        // Role marker overhead plus content.
        total += 2 + estimateTextTokens(message.content);
    }
    for (const auto &definition : request.tools)
    {
        total += 6 + estimateTextTokens(definition.name) + estimateTextTokens(definition.description)
                 + estimateTextTokens(definition.parametersJson);
    }
    for (const auto &exchange : request.toolHistory)
    {
        total += estimateTextTokens(exchange.reasoning) + estimateTextTokens(exchange.reasoningSignature);
        for (const auto &call : exchange.calls)
        {
            total += 4 + estimateTextTokens(call.id) + estimateTextTokens(call.name)
                     + estimateTextTokens(call.argumentsJson);
        }
        for (const auto &output : exchange.outputs)
        {
            total += 4 + estimateTextTokens(output.callId) + estimateTextTokens(output.name)
                     + estimateTextTokens(output.outputJson);
        }
    }
    return total;
}

AiCompactionResult AiContextCompactor::compact(AiGenerationRequest request, const AiCompactionLimits &limits)
{
    const auto usableTokens =
        limits.contextWindowTokens > limits.reservedOutputTokens + limits.reserveBufferTokens
            ? limits.contextWindowTokens - limits.reservedOutputTokens - limits.reserveBufferTokens
            : std::size_t{1};
    auto estimated = estimateRequestTokens(request);
    if (estimated <= usableTokens)
    {
        return AiCompactionResult{.request = std::move(request),
                                  .estimatedInputTokens = estimated,
                                  .compacted = false,
                                  .overBudget = false};
    }

    // Preserve the recent tail verbatim; truncate everything older.
    const std::size_t preserved =
        std::min(limits.preserveRecentMessages, static_cast<std::size_t>(request.messages.size()));
    const std::size_t oldCount = request.messages.size() - preserved;
    std::size_t compactedMessages = 0;
    std::size_t compactedCharacters = 0;
    for (std::size_t index = 0; index < oldCount; ++index)
    {
        auto &message = request.messages[index];
        if (message.content.size()
            <= limits.oldMessageHeadCharacters + limits.oldMessageTailCharacters + std::size_t{32})
        {
            continue;
        }
        const auto head = truncatedPrefix(message.content, limits.oldMessageHeadCharacters);
        const auto tail = truncatedSuffix(message.content, limits.oldMessageTailCharacters);
        compactedCharacters += message.content.size() - (head.size() + tail.size());
        message.content.clear();
        message.content.reserve(head.size() + tail.size() + std::size_t{32});
        message.content += head;
        message.content += "\n...[older content truncated]...\n";
        message.content += tail;
        ++compactedMessages;
    }
    // Tool results in retained exchanges get a tighter cap so the transcript
    // history cannot dominate the window.
    for (auto &exchange : request.toolHistory)
    {
        for (auto &output : exchange.outputs)
        {
            if (output.outputJson.size() > limits.maximumToolOutputCharacters)
            {
                output.outputJson =
                    truncatedPrefix(output.outputJson, limits.maximumToolOutputCharacters) + "\n...[truncated]...";
                ++compactedMessages;
            }
        }
    }

    estimated = estimateRequestTokens(request);
    const bool overBudget = estimated > usableTokens;
    return AiCompactionResult{.request = std::move(request),
                              .estimatedInputTokens = estimated,
                              .compactedMessageCount = compactedMessages,
                              .compactedCharacters = compactedCharacters,
                              .compacted = compactedMessages > 0,
                              .overBudget = overBudget};
}

} // namespace ztermy::ai
