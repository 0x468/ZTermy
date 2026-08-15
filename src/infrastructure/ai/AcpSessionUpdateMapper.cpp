#include "infrastructure/ai/AcpSessionUpdateMapper.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>
#include <limits>

namespace ztermy::ai
{
namespace
{

constexpr qsizetype maximumTextChunkBytes = qsizetype{512} * 1024;
constexpr qsizetype maximumToolPayloadBytes = qsizetype{256} * 1024;
constexpr qsizetype maximumTrackedTools = 64;
constexpr double maximumJsonInteger = 9'007'199'254'740'991.0;

[[nodiscard]] std::string utf8(const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] std::expected<std::string, QString> compactJson(const QJsonValue &value)
{
    if (value.isUndefined())
    {
        return std::string{};
    }
    QByteArray encoded;
    if (value.isObject())
    {
        encoded = QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    }
    else if (value.isArray())
    {
        encoded = QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    }
    else
    {
        encoded = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
        encoded = encoded.mid(1, encoded.size() - 2);
    }
    if (encoded.size() > maximumToolPayloadBytes)
    {
        return std::unexpected(QStringLiteral("An ACP tool payload exceeded 256 KiB."));
    }
    return std::string(encoded.constData(), static_cast<std::size_t>(encoded.size()));
}

[[nodiscard]] std::expected<std::uint64_t, QString> unsignedInteger(const QJsonValue &value, const QString &name)
{
    if (!value.isDouble())
    {
        return std::unexpected(QStringLiteral("ACP %1 must be an unsigned integer.").arg(name));
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number || number > maximumJsonInteger)
    {
        return std::unexpected(QStringLiteral("ACP %1 is outside the supported integer range.").arg(name));
    }
    return static_cast<std::uint64_t>(number);
}

[[nodiscard]] std::expected<std::string, QString> mappedToolState(const QString &status)
{
    if (status == QStringLiteral("pending"))
    {
        return std::string{"pending"};
    }
    if (status == QStringLiteral("in_progress"))
    {
        return std::string{"running"};
    }
    if (status == QStringLiteral("completed"))
    {
        return std::string{"succeeded"};
    }
    if (status == QStringLiteral("failed"))
    {
        return std::string{"failed"};
    }
    return std::unexpected(QStringLiteral("The ACP tool call has an invalid status."));
}

[[nodiscard]] bool sideEffectingKind(const QString &kind)
{
    return kind == QStringLiteral("edit") || kind == QStringLiteral("delete") || kind == QStringLiteral("move")
           || kind == QStringLiteral("execute") || kind == QStringLiteral("switch_mode");
}

} // namespace

std::expected<AcpMappedSessionUpdate, QString> AcpSessionUpdateMapper::map(const AcpMessage &message)
{
    if (message.kind != AcpMessageKind::notification || message.method != QStringLiteral("session/update")
        || !message.params.value(QStringLiteral("update")).isObject())
    {
        return std::unexpected(QStringLiteral("Only ACP session/update notifications can be mapped."));
    }

    const QJsonObject update = message.params.value(QStringLiteral("update")).toObject();
    const QString updateType = update.value(QStringLiteral("sessionUpdate")).toString();
    if (updateType.isEmpty())
    {
        return std::unexpected(QStringLiteral("The ACP session update has no type."));
    }

    AcpMappedSessionUpdate mapped;
    if (updateType == QStringLiteral("agent_message_chunk") || updateType == QStringLiteral("agent_thought_chunk"))
    {
        const QJsonObject content = update.value(QStringLiteral("content")).toObject();
        const QString chunk = content.value(QStringLiteral("text")).toString();
        if (content.value(QStringLiteral("type")).toString() != QStringLiteral("text")
            || chunk.toUtf8().size() > maximumTextChunkBytes)
        {
            return std::unexpected(QStringLiteral("The ACP Agent emitted an invalid or oversized text chunk."));
        }
        if (!chunk.isEmpty())
        {
            mapped.streamEvents.push_back(AiStreamEvent{.type = updateType == QStringLiteral("agent_message_chunk")
                                                                    ? AiStreamEventType::textDelta
                                                                    : AiStreamEventType::reasoningDelta,
                                                        .delta = utf8(chunk)});
        }
        return mapped;
    }
    if (updateType == QStringLiteral("tool_call") || updateType == QStringLiteral("tool_call_update"))
    {
        auto activity = mapToolCall(update, updateType == QStringLiteral("tool_call"));
        if (!activity.has_value())
        {
            return std::unexpected(activity.error());
        }
        mapped.toolActivity = std::move(*activity);
        return mapped;
    }
    if (updateType == QStringLiteral("usage_update"))
    {
        auto used = unsignedInteger(update.value(QStringLiteral("used")), QStringLiteral("usage used"));
        auto size = unsignedInteger(update.value(QStringLiteral("size")), QStringLiteral("usage size"));
        if (!used.has_value() || !size.has_value() || *used > *size)
        {
            return std::unexpected(!used.has_value()   ? used.error()
                                   : !size.has_value() ? size.error()
                                                       : QStringLiteral("ACP context usage cannot exceed its size."));
        }
        AcpUsageUpdate usage{.used = *used, .size = *size};
        const QJsonObject cost = update.value(QStringLiteral("cost")).toObject();
        if (!cost.isEmpty())
        {
            const double amount = cost.value(QStringLiteral("amount")).toDouble(-1.0);
            const QString currency = cost.value(QStringLiteral("currency")).toString();
            if (!std::isfinite(amount) || amount < 0.0 || currency.isEmpty() || currency.size() > 16)
            {
                return std::unexpected(QStringLiteral("The ACP usage cost is invalid."));
            }
            usage.costAmount = amount;
            usage.costCurrency = currency;
        }
        mapped.usage = std::move(usage);
        return mapped;
    }

    // Mode, model, plan and available-command updates are session metadata,
    // not assistant prose. They are intentionally ignored until their typed
    // UI surfaces are connected; unknown future update kinds remain forward-compatible.
    return mapped;
}

void AcpSessionUpdateMapper::reset() noexcept
{
    m_tools.clear();
}

std::expected<std::optional<AiToolActivity>, QString> AcpSessionUpdateMapper::mapToolCall(const QJsonObject &update,
                                                                                          const bool initial)
{
    const QString id = update.value(QStringLiteral("toolCallId")).toString();
    if (id.isEmpty() || id.size() > 512)
    {
        return std::unexpected(QStringLiteral("The ACP tool call has an invalid id."));
    }

    const auto existing = m_tools.constFind(id);
    if (initial && existing != m_tools.cend())
    {
        return std::unexpected(QStringLiteral("The ACP Agent repeated a tool_call id."));
    }
    if (!initial && existing == m_tools.cend())
    {
        return std::unexpected(QStringLiteral("The ACP Agent updated an unknown tool call."));
    }
    if (initial && m_tools.size() >= maximumTrackedTools)
    {
        return std::unexpected(QStringLiteral("The ACP Agent emitted too many tool calls in one prompt."));
    }

    AiToolActivity activity = existing == m_tools.cend() ? AiToolActivity{} : existing.value();
    activity.id = utf8(id);
    if (update.contains(QStringLiteral("title")))
    {
        const QString title = update.value(QStringLiteral("title")).toString();
        if (title.isEmpty() || title.toUtf8().size() > 4096)
        {
            return std::unexpected(QStringLiteral("The ACP tool call has an invalid title."));
        }
        activity.summary = utf8(title);
    }
    if (update.contains(QStringLiteral("kind")))
    {
        const QString kind = update.value(QStringLiteral("kind")).toString();
        if (kind.isEmpty() || kind.size() > 64)
        {
            return std::unexpected(QStringLiteral("The ACP tool call has an invalid kind."));
        }
        activity.name = utf8(kind);
        activity.sideEffecting = sideEffectingKind(kind);
    }
    if (update.contains(QStringLiteral("status")))
    {
        auto state = mappedToolState(update.value(QStringLiteral("status")).toString());
        if (!state.has_value())
        {
            return std::unexpected(state.error());
        }
        activity.state = std::move(*state);
    }
    if (update.contains(QStringLiteral("rawInput")))
    {
        auto input = compactJson(update.value(QStringLiteral("rawInput")));
        if (!input.has_value())
        {
            return std::unexpected(input.error());
        }
        activity.argumentsJson = std::move(*input);
    }
    if (update.contains(QStringLiteral("rawOutput")))
    {
        auto output = compactJson(update.value(QStringLiteral("rawOutput")));
        if (!output.has_value())
        {
            return std::unexpected(output.error());
        }
        activity.resultJson = std::move(*output);
    }
    else if (update.contains(QStringLiteral("content")))
    {
        auto output = compactJson(update.value(QStringLiteral("content")));
        if (!output.has_value())
        {
            return std::unexpected(output.error());
        }
        activity.resultJson = std::move(*output);
    }

    if (initial && (activity.summary.empty() || activity.name.empty() || activity.state.empty()))
    {
        return std::unexpected(QStringLiteral("The initial ACP tool call is incomplete."));
    }
    m_tools.insert(id, activity);
    return std::optional<AiToolActivity>{std::move(activity)};
}

} // namespace ztermy::ai
