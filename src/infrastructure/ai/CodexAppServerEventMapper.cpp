#include "infrastructure/ai/CodexAppServerEventMapper.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace ztermy::ai
{
namespace
{

constexpr qsizetype maximumDeltaBytes = qsizetype{256} * 1024;
constexpr qsizetype maximumIdentifierBytes = 256;
constexpr qsizetype maximumErrorBytes = qsizetype{16} * 1024;
constexpr double maximumExactJsonInteger = 9'007'199'254'740'991.0;

[[nodiscard]] std::string bytes(const QString &value)
{
    const QByteArray encoded = value.toUtf8();
    return {encoded.constData(), static_cast<std::size_t>(encoded.size())};
}

[[nodiscard]] bool boundedPayload(const QString &value, const qsizetype maximumBytes, const bool allowEmpty = false)
{
    const qsizetype size = value.toUtf8().size();
    return (allowEmpty || !value.isEmpty()) && size <= maximumBytes && !value.contains(QChar::ReplacementCharacter)
           && !value.contains(QChar(u'\0'));
}

[[nodiscard]] bool boundedIdentifier(const QString &value)
{
    if (!boundedPayload(value, maximumIdentifierBytes))
    {
        return false;
    }
    for (const QChar character : value)
    {
        const ushort code = character.unicode();
        if (code < ushort{0x20} || code == ushort{0x7F})
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::uint64_t> unsignedInteger(const QJsonValue &value)
{
    if (!value.isDouble())
    {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number || number > maximumExactJsonInteger)
    {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(number);
}

[[nodiscard]] std::optional<AiTokenUsage> usage(const QJsonObject &params)
{
    const QJsonObject last =
        params.value(QStringLiteral("tokenUsage")).toObject().value(QStringLiteral("last")).toObject();
    const auto input = unsignedInteger(last.value(QStringLiteral("inputTokens")));
    const auto output = unsignedInteger(last.value(QStringLiteral("outputTokens")));
    const auto reasoning = unsignedInteger(last.value(QStringLiteral("reasoningOutputTokens")));
    const auto cached = unsignedInteger(last.value(QStringLiteral("cachedInputTokens")));
    if (!input.has_value() || !output.has_value() || !reasoning.has_value() || !cached.has_value())
    {
        return std::nullopt;
    }
    return AiTokenUsage{.inputTokens = *input,
                        .outputTokens = *output,
                        .reasoningTokens = *reasoning,
                        .cachedInputTokens = *cached};
}

[[nodiscard]] AiProviderErrorCode errorCode(const QJsonValue &value)
{
    QString code = value.toString();
    if (code.isEmpty() && value.isObject() && !value.toObject().isEmpty())
    {
        code = value.toObject().constBegin().key();
    }
    if (code == QStringLiteral("unauthorized"))
    {
        return AiProviderErrorCode::authentication;
    }
    if (code == QStringLiteral("usageLimitExceeded") || code == QStringLiteral("sessionBudgetExceeded"))
    {
        return AiProviderErrorCode::quotaExceeded;
    }
    if (code == QStringLiteral("contextWindowExceeded"))
    {
        return AiProviderErrorCode::contextOverflow;
    }
    if (code == QStringLiteral("httpConnectionFailed") || code == QStringLiteral("responseStreamConnectionFailed")
        || code == QStringLiteral("responseStreamDisconnected"))
    {
        return AiProviderErrorCode::network;
    }
    if (code == QStringLiteral("badRequest"))
    {
        return AiProviderErrorCode::invalidRequest;
    }
    if (code == QStringLiteral("serverOverloaded") || code == QStringLiteral("internalServerError")
        || code == QStringLiteral("responseTooManyFailedAttempts"))
    {
        return AiProviderErrorCode::server;
    }
    return AiProviderErrorCode::protocol;
}

[[nodiscard]] AiProviderError providerError(const QJsonObject &value, const QString &fallback)
{
    QString message = value.value(QStringLiteral("message")).toString();
    if (!boundedPayload(message, maximumErrorBytes))
    {
        message = fallback;
    }
    const AiProviderErrorCode code = errorCode(value.value(QStringLiteral("codexErrorInfo")));
    return {.code = code,
            .message = bytes(message),
            .retryable = code == AiProviderErrorCode::network || code == AiProviderErrorCode::server};
}

[[nodiscard]] std::expected<void, QString> validateDelta(const QString &itemId, const QString &delta)
{
    if (!boundedIdentifier(itemId) || !boundedPayload(delta, maximumDeltaBytes, true))
    {
        return std::unexpected(QStringLiteral("A Codex stream delta exceeded its bounds."));
    }
    return {};
}

} // namespace

std::expected<std::vector<AiStreamEvent>, QString> CodexAppServerEventMapper::map(const CodexAppServerMessage &message)
{
    if (message.kind != CodexAppServerMessageKind::notification)
    {
        return std::vector<AiStreamEvent>{};
    }
    return mapNotification(message);
}

void CodexAppServerEventMapper::reset() noexcept
{
    m_textItemsWithDeltas.clear();
    m_reasoningItemsWithSummary.clear();
    m_bufferedRawReasoning.clear();
    m_pendingError.reset();
    m_turnId.clear();
}

std::expected<std::vector<AiStreamEvent>, QString>
CodexAppServerEventMapper::mapNotification(const CodexAppServerMessage &message)
{
    if (message.method == QStringLiteral("turn/started"))
    {
        reset();
        m_turnId = message.params.value(QStringLiteral("turn")).toObject().value(QStringLiteral("id")).toString();
        if (!boundedIdentifier(m_turnId))
        {
            return std::unexpected(QStringLiteral("Codex started a turn without a bounded id."));
        }
        return std::vector{AiStreamEvent{.type = AiStreamEventType::responseStarted, .responseId = bytes(m_turnId)}};
    }
    if (message.method == QStringLiteral("item/agentMessage/delta"))
    {
        if (m_turnId.isEmpty())
        {
            return std::unexpected(QStringLiteral("Codex emitted an item outside an active turn."));
        }
        const QString itemId = message.params.value(QStringLiteral("itemId")).toString();
        const QString delta = message.params.value(QStringLiteral("delta")).toString();
        if (const auto valid = validateDelta(itemId, delta); !valid.has_value())
        {
            return std::unexpected(valid.error());
        }
        m_textItemsWithDeltas.insert(itemId);
        return std::vector{
            AiStreamEvent{.type = AiStreamEventType::textDelta, .itemId = bytes(itemId), .delta = bytes(delta)}};
    }
    if (message.method == QStringLiteral("item/reasoning/summaryTextDelta"))
    {
        if (m_turnId.isEmpty())
        {
            return std::unexpected(QStringLiteral("Codex emitted reasoning outside an active turn."));
        }
        const QString itemId = message.params.value(QStringLiteral("itemId")).toString();
        const QString delta = message.params.value(QStringLiteral("delta")).toString();
        if (const auto valid = validateDelta(itemId, delta); !valid.has_value())
        {
            return std::unexpected(valid.error());
        }
        m_reasoningItemsWithSummary.insert(itemId);
        m_bufferedRawReasoning.remove(itemId);
        return std::vector{
            AiStreamEvent{.type = AiStreamEventType::reasoningDelta, .itemId = bytes(itemId), .delta = bytes(delta)}};
    }
    if (message.method == QStringLiteral("item/reasoning/textDelta"))
    {
        if (m_turnId.isEmpty())
        {
            return std::unexpected(QStringLiteral("Codex emitted reasoning outside an active turn."));
        }
        const QString itemId = message.params.value(QStringLiteral("itemId")).toString();
        const QString delta = message.params.value(QStringLiteral("delta")).toString();
        if (const auto valid = validateDelta(itemId, delta); !valid.has_value())
        {
            return std::unexpected(valid.error());
        }
        if (!m_reasoningItemsWithSummary.contains(itemId))
        {
            QByteArray &buffer = m_bufferedRawReasoning[itemId];
            const QByteArray encoded = delta.toUtf8();
            if (buffer.size() > maximumDeltaBytes - encoded.size())
            {
                return std::unexpected(QStringLiteral("Codex raw reasoning exceeded 256 KiB."));
            }
            buffer.append(encoded);
        }
        return std::vector<AiStreamEvent>{};
    }
    if (message.method == QStringLiteral("item/started"))
    {
        if (m_turnId.isEmpty())
        {
            return std::unexpected(QStringLiteral("Codex started an item outside an active turn."));
        }
        return mapItemStarted(message.params);
    }
    if (message.method == QStringLiteral("item/completed"))
    {
        if (m_turnId.isEmpty())
        {
            return std::unexpected(QStringLiteral("Codex completed an item outside an active turn."));
        }
        return mapItemCompleted(message.params);
    }
    if (message.method == QStringLiteral("thread/tokenUsage/updated"))
    {
        if (m_turnId.isEmpty())
        {
            return std::unexpected(QStringLiteral("Codex emitted token usage outside an active turn."));
        }
        const auto value = usage(message.params);
        if (!value.has_value())
        {
            return std::unexpected(QStringLiteral("Codex emitted invalid token usage."));
        }
        return std::vector{AiStreamEvent{.type = AiStreamEventType::usageUpdated, .usage = *value}};
    }
    if (message.method == QStringLiteral("error"))
    {
        if (m_turnId.isEmpty())
        {
            return std::unexpected(QStringLiteral("Codex emitted a turn error outside an active turn."));
        }
        const QJsonObject error = message.params.value(QStringLiteral("error")).toObject();
        m_pendingError = providerError(error, QStringLiteral("The Codex turn failed."));
        return std::vector<AiStreamEvent>{};
    }
    if (message.method == QStringLiteral("turn/completed"))
    {
        return mapTurnCompleted(message.params);
    }
    return std::vector<AiStreamEvent>{};
}

std::expected<std::vector<AiStreamEvent>, QString> CodexAppServerEventMapper::mapItemStarted(const QJsonObject &params)
{
    const QJsonObject item = params.value(QStringLiteral("item")).toObject();
    const QString type = item.value(QStringLiteral("type")).toString();
    const QString itemId = item.value(QStringLiteral("id")).toString();
    if (!boundedIdentifier(itemId))
    {
        return std::unexpected(QStringLiteral("Codex started an item without a bounded id."));
    }
    if (type == QStringLiteral("dynamicToolCall"))
    {
        const QString tool = item.value(QStringLiteral("tool")).toString();
        if (!boundedIdentifier(tool) || !item.value(QStringLiteral("arguments")).isObject())
        {
            return std::unexpected(QStringLiteral("Codex started an invalid dynamic tool item."));
        }
        const QByteArray arguments =
            QJsonDocument(item.value(QStringLiteral("arguments")).toObject()).toJson(QJsonDocument::Compact);
        if (arguments.size() > maximumDeltaBytes)
        {
            return std::unexpected(QStringLiteral("Codex dynamic tool arguments exceeded 256 KiB."));
        }
        return std::vector{AiStreamEvent{.type = AiStreamEventType::toolCallStarted,
                                         .itemId = bytes(itemId),
                                         .toolCallId = bytes(itemId),
                                         .toolName = bytes(tool)},
                           AiStreamEvent{.type = AiStreamEventType::toolArgumentsDelta,
                                         .itemId = bytes(itemId),
                                         .toolCallId = bytes(itemId),
                                         .toolName = bytes(tool),
                                         .delta = arguments.toStdString()}};
    }
    if (type == QStringLiteral("webSearch"))
    {
        const QString query = item.value(QStringLiteral("query")).toString();
        std::vector<AiStreamEvent> events{
            AiStreamEvent{.type = AiStreamEventType::webSearchStarted, .itemId = bytes(itemId)}};
        if (!query.isEmpty() && !boundedPayload(query, maximumDeltaBytes))
        {
            return std::unexpected(QStringLiteral("A Codex web search query exceeded its bounds."));
        }
        if (!query.isEmpty())
        {
            events.push_back(AiStreamEvent{.type = AiStreamEventType::webSearchQuery,
                                           .itemId = bytes(itemId),
                                           .delta = bytes(query)});
        }
        return events;
    }
    return std::vector<AiStreamEvent>{};
}

std::expected<std::vector<AiStreamEvent>, QString>
CodexAppServerEventMapper::mapItemCompleted(const QJsonObject &params)
{
    const QJsonObject item = params.value(QStringLiteral("item")).toObject();
    const QString type = item.value(QStringLiteral("type")).toString();
    const QString itemId = item.value(QStringLiteral("id")).toString();
    if (!boundedIdentifier(itemId))
    {
        return std::unexpected(QStringLiteral("Codex completed an item without a bounded id."));
    }
    if (type == QStringLiteral("agentMessage") && !m_textItemsWithDeltas.contains(itemId))
    {
        const QString textValue = item.value(QStringLiteral("text")).toString();
        if (!boundedPayload(textValue, maximumDeltaBytes, true))
        {
            return std::unexpected(QStringLiteral("A completed Codex message exceeded 256 KiB."));
        }
        return std::vector{
            AiStreamEvent{.type = AiStreamEventType::textDelta, .itemId = bytes(itemId), .delta = bytes(textValue)}};
    }
    if (type == QStringLiteral("reasoning") && !m_reasoningItemsWithSummary.contains(itemId))
    {
        const QByteArray buffered = m_bufferedRawReasoning.take(itemId);
        if (!buffered.isEmpty())
        {
            return std::vector{
                AiStreamEvent{.type = AiStreamEventType::reasoningDelta,
                              .itemId = bytes(itemId),
                              .delta = std::string(buffered.constData(), static_cast<std::size_t>(buffered.size()))}};
        }
    }
    if (type == QStringLiteral("dynamicToolCall"))
    {
        const QString tool = item.value(QStringLiteral("tool")).toString();
        if (!boundedIdentifier(tool))
        {
            return std::unexpected(QStringLiteral("Codex completed an invalid dynamic tool item."));
        }
        return std::vector{AiStreamEvent{.type = AiStreamEventType::toolCallCompleted,
                                         .itemId = bytes(itemId),
                                         .toolCallId = bytes(itemId),
                                         .toolName = bytes(tool)}};
    }
    if (type == QStringLiteral("webSearch"))
    {
        return std::vector{AiStreamEvent{.type = AiStreamEventType::webSearchCompleted, .itemId = bytes(itemId)}};
    }
    return std::vector<AiStreamEvent>{};
}

std::expected<std::vector<AiStreamEvent>, QString>
CodexAppServerEventMapper::mapTurnCompleted(const QJsonObject &params)
{
    const QJsonObject turn = params.value(QStringLiteral("turn")).toObject();
    const QString id = turn.value(QStringLiteral("id")).toString();
    const QString status = turn.value(QStringLiteral("status")).toString();
    if (m_turnId.isEmpty() || !boundedIdentifier(id) || id != m_turnId)
    {
        return std::unexpected(QStringLiteral("Codex completed a different or invalid turn."));
    }
    std::vector<AiStreamEvent> events;
    if (status == QStringLiteral("completed"))
    {
        events.push_back(AiStreamEvent{.type = AiStreamEventType::responseCompleted,
                                       .responseId = bytes(id),
                                       .stopReason = AiResponseStopReason::endTurn});
    }
    else if (status == QStringLiteral("interrupted"))
    {
        events.push_back(AiStreamEvent{.type = AiStreamEventType::responseFailed,
                                       .responseId = bytes(id),
                                       .error = AiProviderError{.code = AiProviderErrorCode::cancelled,
                                                                .message = "The Codex turn was cancelled."}});
    }
    else if (status == QStringLiteral("failed"))
    {
        const QJsonObject error = turn.value(QStringLiteral("error")).toObject();
        events.push_back(AiStreamEvent{
            .type = AiStreamEventType::responseFailed,
            .responseId = bytes(id),
            .error = !error.isEmpty() ? providerError(error, QStringLiteral("The Codex turn failed."))
                                      : m_pendingError.value_or(AiProviderError{.code = AiProviderErrorCode::protocol,
                                                                                .message = "The Codex turn failed."})});
    }
    else
    {
        return std::unexpected(QStringLiteral("Codex completed a turn with an invalid status."));
    }
    reset();
    return events;
}

} // namespace ztermy::ai
