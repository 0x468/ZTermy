#include "domain/ai/AiProviderReplayCodec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QString>

#include <algorithm>
#include <utility>

namespace ztermy::ai
{
namespace
{
constexpr qsizetype maximumExchanges = 64;
constexpr qsizetype maximumCallsPerExchange = 64;
constexpr qsizetype maximumOutputsPerExchange = 64;
constexpr std::size_t maximumIdBytes = 256;
constexpr std::size_t maximumNameBytes = 128;
constexpr std::size_t maximumPayloadBytes = std::size_t{64} * 1024;
constexpr std::size_t maximumReasoningBytes = std::size_t{128} * 1024;

[[nodiscard]] QString text(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::string bytes(const QByteArray &value)
{
    return {value.constData(), static_cast<std::size_t>(value.size())};
}

[[nodiscard]] bool hasOnlyKeys(const QJsonObject &object, const QSet<QString> &keys)
{
    return std::ranges::all_of(object.keys(), [&keys](const QString &key) {
        return keys.contains(key);
    });
}

[[nodiscard]] bool bounded(const std::string_view value, const std::size_t maximum) noexcept
{
    return value.size() <= maximum;
}

[[nodiscard]] std::expected<QJsonArray, AiProviderReplayError> contentArray(const std::string_view json)
{
    if (json.empty())
    {
        return QJsonArray{};
    }
    if (json.size() > AiProviderReplayCodec::maximumBytes)
    {
        return std::unexpected(AiProviderReplayError::limitExceeded);
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(QByteArray(json.data(), static_cast<qsizetype>(json.size())), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()
        || !std::ranges::all_of(document.array(), [](const QJsonValue &value) {
               return value.isObject();
           }))
    {
        return std::unexpected(AiProviderReplayError::invalidData);
    }
    return document.array();
}

[[nodiscard]] std::expected<AiToolExchange, AiProviderReplayError> decodeExchange(const QJsonObject &object)
{
    static const QSet<QString> keys{QStringLiteral("calls"), QStringLiteral("outputs"), QStringLiteral("reasoning"),
                                    QStringLiteral("reasoningSignature"), QStringLiteral("assistantContent")};
    if (!hasOnlyKeys(object, keys))
    {
        return std::unexpected(AiProviderReplayError::invalidData);
    }
    const QJsonArray calls = object.value(QStringLiteral("calls")).toArray();
    const QJsonArray outputs = object.value(QStringLiteral("outputs")).toArray();
    const QString reasoning = object.value(QStringLiteral("reasoning")).toString();
    const QString reasoningSignature = object.value(QStringLiteral("reasoningSignature")).toString();
    if (calls.size() > maximumCallsPerExchange || outputs.size() > maximumOutputsPerExchange
        || std::cmp_greater(reasoning.toUtf8().size(), maximumReasoningBytes)
        || std::cmp_greater(reasoningSignature.toUtf8().size(), maximumReasoningBytes))
    {
        return std::unexpected(AiProviderReplayError::limitExceeded);
    }

    AiToolExchange exchange{.reasoning = reasoning.toUtf8().toStdString(),
                            .reasoningSignature = reasoningSignature.toUtf8().toStdString()};
    exchange.calls.reserve(static_cast<std::size_t>(calls.size()));
    for (const QJsonValue &value : calls)
    {
        const QJsonObject call = value.toObject();
        static const QSet<QString> callKeys{QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("arguments")};
        const QByteArray id = call.value(QStringLiteral("id")).toString().toUtf8();
        const QByteArray name = call.value(QStringLiteral("name")).toString().toUtf8();
        const QByteArray arguments = call.value(QStringLiteral("arguments")).toString().toUtf8();
        if (!value.isObject() || !hasOnlyKeys(call, callKeys) || id.isEmpty() || name.isEmpty()
            || std::cmp_greater(id.size(), maximumIdBytes) || std::cmp_greater(name.size(), maximumNameBytes)
            || std::cmp_greater(arguments.size(), maximumPayloadBytes))
        {
            return std::unexpected(AiProviderReplayError::invalidData);
        }
        exchange.calls.push_back(
            {.id = id.toStdString(), .name = name.toStdString(), .argumentsJson = arguments.toStdString()});
    }
    exchange.outputs.reserve(static_cast<std::size_t>(outputs.size()));
    for (const QJsonValue &value : outputs)
    {
        const QJsonObject output = value.toObject();
        static const QSet<QString> outputKeys{QStringLiteral("callId"), QStringLiteral("name"),
                                              QStringLiteral("output")};
        const QByteArray callId = output.value(QStringLiteral("callId")).toString().toUtf8();
        const QByteArray name = output.value(QStringLiteral("name")).toString().toUtf8();
        const QByteArray payload = output.value(QStringLiteral("output")).toString().toUtf8();
        if (!value.isObject() || !hasOnlyKeys(output, outputKeys) || callId.isEmpty() || name.isEmpty()
            || std::cmp_greater(callId.size(), maximumIdBytes) || std::cmp_greater(name.size(), maximumNameBytes)
            || std::cmp_greater(payload.size(), maximumPayloadBytes))
        {
            return std::unexpected(AiProviderReplayError::invalidData);
        }
        exchange.outputs.push_back(
            {.callId = callId.toStdString(), .name = name.toStdString(), .outputJson = payload.toStdString()});
    }
    const QJsonValue assistantContent = object.value(QStringLiteral("assistantContent"));
    if (!assistantContent.isUndefined())
    {
        if (!assistantContent.isArray()
            || !std::ranges::all_of(assistantContent.toArray(), [](const QJsonValue &value) {
                   return value.isObject();
               }))
        {
            return std::unexpected(AiProviderReplayError::invalidData);
        }
        exchange.providerAssistantContentJson =
            bytes(QJsonDocument(assistantContent.toArray()).toJson(QJsonDocument::Compact));
    }
    return exchange;
}

} // namespace

std::expected<std::string, AiProviderReplayError>
AiProviderReplayCodec::encode(const std::span<const AiToolExchange> toolHistory,
                              const std::string_view finalAssistantContentJson)
{
    if (toolHistory.empty() && finalAssistantContentJson.empty())
    {
        return std::string{};
    }
    if (toolHistory.size() > static_cast<std::size_t>(maximumExchanges))
    {
        return std::unexpected(AiProviderReplayError::limitExceeded);
    }
    const auto finalContent = contentArray(finalAssistantContentJson);
    if (!finalContent.has_value())
    {
        return std::unexpected(finalContent.error());
    }

    QJsonArray exchanges;
    for (const AiToolExchange &exchange : toolHistory)
    {
        if (exchange.calls.size() > static_cast<std::size_t>(maximumCallsPerExchange)
            || exchange.outputs.size() > static_cast<std::size_t>(maximumOutputsPerExchange)
            || !bounded(exchange.reasoning, maximumReasoningBytes)
            || !bounded(exchange.reasoningSignature, maximumReasoningBytes))
        {
            return std::unexpected(AiProviderReplayError::limitExceeded);
        }
        QJsonArray calls;
        for (const AiToolCall &call : exchange.calls)
        {
            if (call.id.empty() || call.name.empty() || !bounded(call.id, maximumIdBytes)
                || !bounded(call.name, maximumNameBytes) || !bounded(call.argumentsJson, maximumPayloadBytes))
            {
                return std::unexpected(AiProviderReplayError::invalidData);
            }
            calls.push_back(QJsonObject{{QStringLiteral("id"), text(call.id)},
                                        {QStringLiteral("name"), text(call.name)},
                                        {QStringLiteral("arguments"), text(call.argumentsJson)}});
        }
        QJsonArray outputs;
        for (const AiToolOutput &output : exchange.outputs)
        {
            if (output.callId.empty() || output.name.empty() || !bounded(output.callId, maximumIdBytes)
                || !bounded(output.name, maximumNameBytes) || !bounded(output.outputJson, maximumPayloadBytes))
            {
                return std::unexpected(AiProviderReplayError::invalidData);
            }
            outputs.push_back(QJsonObject{{QStringLiteral("callId"), text(output.callId)},
                                          {QStringLiteral("name"), text(output.name)},
                                          {QStringLiteral("output"), text(output.outputJson)}});
        }
        QJsonObject value{{QStringLiteral("calls"), calls},
                          {QStringLiteral("outputs"), outputs},
                          {QStringLiteral("reasoning"), text(exchange.reasoning)},
                          {QStringLiteral("reasoningSignature"), text(exchange.reasoningSignature)}};
        const auto assistantContent = contentArray(exchange.providerAssistantContentJson);
        if (!assistantContent.has_value())
        {
            return std::unexpected(assistantContent.error());
        }
        if (!assistantContent->isEmpty())
        {
            value.insert(QStringLiteral("assistantContent"), *assistantContent);
        }
        exchanges.push_back(value);
    }

    QJsonObject root{{QStringLiteral("version"), 1}, {QStringLiteral("toolHistory"), exchanges}};
    if (!finalContent->isEmpty())
    {
        root.insert(QStringLiteral("finalAssistantContent"), *finalContent);
    }
    const QByteArray result = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (std::cmp_greater(result.size(), maximumBytes))
    {
        return std::unexpected(AiProviderReplayError::limitExceeded);
    }
    return bytes(result);
}

std::expected<AiProviderReplay, AiProviderReplayError> AiProviderReplayCodec::decode(const std::string_view json)
{
    if (json.empty())
    {
        return AiProviderReplay{};
    }
    if (json.size() > maximumBytes)
    {
        return std::unexpected(AiProviderReplayError::limitExceeded);
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(QByteArray(json.data(), static_cast<qsizetype>(json.size())), &error);
    const QJsonObject root = document.object();
    static const QSet<QString> keys{QStringLiteral("version"), QStringLiteral("toolHistory"),
                                    QStringLiteral("finalAssistantContent")};
    const QJsonArray exchanges = root.value(QStringLiteral("toolHistory")).toArray();
    if (error.error != QJsonParseError::NoError || !document.isObject() || !hasOnlyKeys(root, keys)
        || root.value(QStringLiteral("version")).toInt() != 1 || !root.value(QStringLiteral("toolHistory")).isArray()
        || exchanges.size() > maximumExchanges)
    {
        return std::unexpected(AiProviderReplayError::invalidData);
    }
    AiProviderReplay replay;
    replay.toolHistory.reserve(static_cast<std::size_t>(exchanges.size()));
    for (const QJsonValue &value : exchanges)
    {
        if (!value.isObject())
        {
            return std::unexpected(AiProviderReplayError::invalidData);
        }
        auto exchange = decodeExchange(value.toObject());
        if (!exchange.has_value())
        {
            return std::unexpected(exchange.error());
        }
        replay.toolHistory.push_back(std::move(*exchange));
    }
    const QJsonValue finalContent = root.value(QStringLiteral("finalAssistantContent"));
    if (!finalContent.isUndefined())
    {
        if (!finalContent.isArray() || !std::ranges::all_of(finalContent.toArray(), [](const QJsonValue &value) {
                return value.isObject();
            }))
        {
            return std::unexpected(AiProviderReplayError::invalidData);
        }
        replay.finalAssistantContentJson = bytes(QJsonDocument(finalContent.toArray()).toJson(QJsonDocument::Compact));
    }
    if (replay.toolHistory.empty() && replay.finalAssistantContentJson.empty())
    {
        return std::unexpected(AiProviderReplayError::invalidData);
    }
    return replay;
}

} // namespace ztermy::ai
