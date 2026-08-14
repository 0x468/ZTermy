#include "infrastructure/ai/ProviderStreamMapper.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <optional>
#include <utility>

namespace ztermy::ai
{
namespace
{

[[nodiscard]] std::string utf8(const QString &value)
{
    const auto bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] std::expected<QJsonObject, AiProviderError> parseObject(const std::string_view json)
{
    QJsonParseError parseError;
    const auto document =
        QJsonDocument::fromJson(QByteArray(json.data(), static_cast<qsizetype>(json.size())), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(AiProviderError{.code = AiProviderErrorCode::protocol,
                                               .message = "Provider returned malformed JSON.",
                                               .retryable = false});
    }
    return document.object();
}

[[nodiscard]] AiProviderErrorCode classifyProviderError(const QString &code)
{
    if (code.contains("auth", Qt::CaseInsensitive) || code.contains("api_key", Qt::CaseInsensitive))
    {
        return AiProviderErrorCode::authentication;
    }
    if (code.contains("quota", Qt::CaseInsensitive))
    {
        return AiProviderErrorCode::quotaExceeded;
    }
    if (code.contains("rate", Qt::CaseInsensitive))
    {
        return AiProviderErrorCode::rateLimited;
    }
    if (code.contains("server", Qt::CaseInsensitive))
    {
        return AiProviderErrorCode::server;
    }
    if (code.contains("invalid", Qt::CaseInsensitive))
    {
        return AiProviderErrorCode::invalidRequest;
    }
    return AiProviderErrorCode::protocol;
}

[[nodiscard]] AiProviderError providerError(const QJsonValue &value,
                                            const std::string_view fallbackMessage = "Provider request failed.")
{
    const auto object = value.toObject();
    const auto codeText = object.value("code").toString(object.value("type").toString());
    const auto code = classifyProviderError(codeText);
    auto message = utf8(object.value("message").toString());
    if (message.empty())
    {
        message.assign(fallbackMessage);
    }
    return AiProviderError{.code = code,
                           .message = std::move(message),
                           .retryable =
                               code == AiProviderErrorCode::rateLimited || code == AiProviderErrorCode::server};
}

[[nodiscard]] std::optional<AiTokenUsage> openAiUsage(const QJsonObject &object)
{
    if (object.isEmpty())
    {
        return std::nullopt;
    }
    const auto inputDetails = object.value("input_tokens_details").toObject();
    const auto outputDetails = object.value("output_tokens_details").toObject();
    return AiTokenUsage{.inputTokens = object.value("input_tokens").toVariant().toULongLong(),
                        .outputTokens = object.value("output_tokens").toVariant().toULongLong(),
                        .reasoningTokens = outputDetails.value("reasoning_tokens").toVariant().toULongLong(),
                        .cachedInputTokens = inputDetails.value("cached_tokens").toVariant().toULongLong()};
}

void appendUsageAndCompletion(const QJsonObject &response, std::vector<AiStreamEvent> &events)
{
    const auto responseId = utf8(response.value("id").toString());
    if (const auto usage = openAiUsage(response.value("usage").toObject()); usage.has_value())
    {
        events.push_back(
            AiStreamEvent{.type = AiStreamEventType::usageUpdated, .responseId = responseId, .usage = usage});
    }
    events.push_back(AiStreamEvent{.type = AiStreamEventType::responseCompleted, .responseId = responseId});
}

[[nodiscard]] std::string syntheticToolId(const std::uint64_t sequence)
{
    return "ollama-tool-" + std::to_string(sequence);
}

} // namespace

std::expected<std::vector<AiStreamEvent>, AiProviderError>
OpenAiResponsesStreamMapper::map(const ServerSentEvent &event)
{
    const auto parsed = parseObject(event.data);
    if (!parsed.has_value())
    {
        return std::unexpected(parsed.error());
    }

    const auto &object = parsed.value();
    const auto type = object.value("type").toString();
    std::vector<AiStreamEvent> events;
    if (type == "response.created")
    {
        events.push_back(AiStreamEvent{.type = AiStreamEventType::responseStarted,
                                       .responseId = utf8(object.value("response").toObject().value("id").toString())});
    }
    else if (type == "response.output_text.delta" || type == "response.refusal.delta")
    {
        events.push_back(AiStreamEvent{.type = AiStreamEventType::textDelta,
                                       .itemId = utf8(object.value("item_id").toString()),
                                       .delta = utf8(object.value("delta").toString())});
    }
    else if (type == "response.reasoning_summary_text.delta")
    {
        events.push_back(AiStreamEvent{.type = AiStreamEventType::reasoningDelta,
                                       .itemId = utf8(object.value("item_id").toString()),
                                       .delta = utf8(object.value("delta").toString())});
    }
    else if (type == "response.output_item.added")
    {
        const auto item = object.value("item").toObject();
        if (item.value("type").toString() == "function_call")
        {
            const auto itemId = utf8(item.value("id").toString());
            ToolState state{.callId = utf8(item.value("call_id").toString()),
                            .name = utf8(item.value("name").toString())};
            m_toolsByItemId.insert_or_assign(itemId, state);
            events.push_back(AiStreamEvent{.type = AiStreamEventType::toolCallStarted,
                                           .itemId = itemId,
                                           .toolCallId = state.callId,
                                           .toolName = state.name});
        }
    }
    else if (type == "response.function_call_arguments.delta" || type == "response.function_call_arguments.done")
    {
        const auto itemId = utf8(object.value("item_id").toString());
        const auto tool = m_toolsByItemId.find(itemId);
        const auto isDone = type.endsWith(".done");
        events.push_back(
            AiStreamEvent{.type = isDone ? AiStreamEventType::toolCallCompleted : AiStreamEventType::toolArgumentsDelta,
                          .itemId = itemId,
                          .toolCallId = tool == m_toolsByItemId.end() ? std::string{} : tool->second.callId,
                          .toolName = tool == m_toolsByItemId.end() ? std::string{} : tool->second.name,
                          .delta = utf8(object.value(isDone ? "arguments" : "delta").toString())});
    }
    else if (type == "response.completed")
    {
        appendUsageAndCompletion(object.value("response").toObject(), events);
    }
    else if (type == "response.failed" || type == "response.incomplete")
    {
        const auto response = object.value("response").toObject();
        auto error = providerError(response.value("error"), "Provider response was incomplete.");
        events.push_back(AiStreamEvent{.type = AiStreamEventType::responseFailed,
                                       .responseId = utf8(response.value("id").toString()),
                                       .error = std::move(error)});
    }
    else if (type == "error")
    {
        auto error = providerError(object.value("error"));
        events.push_back(AiStreamEvent{.type = AiStreamEventType::responseFailed, .error = std::move(error)});
    }
    return events;
}

void OpenAiResponsesStreamMapper::reset() noexcept
{
    m_toolsByItemId.clear();
}

std::expected<std::vector<AiStreamEvent>, AiProviderError>
OpenAiCompatibleStreamMapper::map(const ServerSentEvent &event)
{
    if (event.data == "[DONE]")
    {
        if (m_completed)
        {
            return std::vector<AiStreamEvent>{};
        }
        m_completed = true;
        std::vector<AiStreamEvent> events;
        for (const auto &[index, tool] : m_toolsByIndex)
        {
            static_cast<void>(index);
            events.push_back(AiStreamEvent{.type = AiStreamEventType::toolCallCompleted,
                                           .toolCallId = tool.callId,
                                           .toolName = tool.name});
        }
        events.push_back(AiStreamEvent{.type = AiStreamEventType::responseCompleted});
        return events;
    }

    const auto parsed = parseObject(event.data);
    if (!parsed.has_value())
    {
        return std::unexpected(parsed.error());
    }
    const auto &object = parsed.value();
    if (object.contains("error"))
    {
        auto error = providerError(object.value("error"));
        return std::vector{AiStreamEvent{.type = AiStreamEventType::responseFailed, .error = std::move(error)}};
    }

    std::vector<AiStreamEvent> events;
    const auto responseId = utf8(object.value("id").toString());
    if (!m_started)
    {
        m_started = true;
        events.push_back(AiStreamEvent{.type = AiStreamEventType::responseStarted, .responseId = responseId});
    }
    const auto choices = object.value("choices").toArray();
    if (!choices.isEmpty())
    {
        const auto choice = choices.first().toObject();
        const auto delta = choice.value("delta").toObject();
        const auto content = utf8(delta.value("content").toString());
        if (!content.empty())
        {
            events.push_back(
                AiStreamEvent{.type = AiStreamEventType::textDelta, .responseId = responseId, .delta = content});
        }
        auto reasoning = utf8(delta.value("reasoning_content").toString());
        if (reasoning.empty())
        {
            reasoning = utf8(delta.value("reasoning").toString());
        }
        if (!reasoning.empty())
        {
            events.push_back(AiStreamEvent{.type = AiStreamEventType::reasoningDelta,
                                           .responseId = responseId,
                                           .delta = std::move(reasoning)});
        }
        const auto toolCalls = delta.value("tool_calls").toArray();
        for (const auto toolValue : toolCalls)
        {
            const auto tool = toolValue.toObject();
            const auto index = static_cast<std::size_t>(tool.value("index").toInt());
            const auto function = tool.value("function").toObject();
            auto &state = m_toolsByIndex[index];
            const auto callId = utf8(tool.value("id").toString());
            const auto name = utf8(function.value("name").toString());
            if (!callId.empty())
            {
                state.callId = callId;
            }
            if (!name.empty())
            {
                state.name = name;
            }
            if (!callId.empty() || !name.empty())
            {
                events.push_back(AiStreamEvent{.type = AiStreamEventType::toolCallStarted,
                                               .responseId = responseId,
                                               .toolCallId = state.callId,
                                               .toolName = state.name});
            }
            const auto arguments = utf8(function.value("arguments").toString());
            if (!arguments.empty())
            {
                events.push_back(AiStreamEvent{.type = AiStreamEventType::toolArgumentsDelta,
                                               .responseId = responseId,
                                               .toolCallId = state.callId,
                                               .toolName = state.name,
                                               .delta = arguments});
            }
        }
        // Usage must be emitted before responseCompleted: the conversation
        // model snapshots usage exactly when the completion event arrives, and
        // emitting it afterwards would surface a zero token count on every
        // reply from OpenAI-compatible endpoints.
        if (const auto usage = openAiUsage(object.value("usage").toObject()); usage.has_value())
        {
            events.push_back(
                AiStreamEvent{.type = AiStreamEventType::usageUpdated, .responseId = responseId, .usage = usage});
        }
        if (!choice.value("finish_reason").isNull() && !m_completed)
        {
            m_completed = true;
            for (const auto &[index, tool] : m_toolsByIndex)
            {
                static_cast<void>(index);
                events.push_back(AiStreamEvent{.type = AiStreamEventType::toolCallCompleted,
                                               .responseId = responseId,
                                               .toolCallId = tool.callId,
                                               .toolName = tool.name});
            }
            events.push_back(AiStreamEvent{.type = AiStreamEventType::responseCompleted, .responseId = responseId});
        }
    }
    return events;
}

void OpenAiCompatibleStreamMapper::reset() noexcept
{
    m_toolsByIndex.clear();
    m_started = false;
    m_completed = false;
}

std::expected<std::vector<AiStreamEvent>, AiProviderError> AnthropicStreamMapper::map(const ServerSentEvent &event)
{
    const auto parsed = parseObject(event.data);
    if (!parsed.has_value())
    {
        return std::unexpected(parsed.error());
    }
    const auto &object = parsed.value();
    const auto type =
        object.value("type").toString(event.event.empty() ? QString{} : QString::fromStdString(event.event));
    std::vector<AiStreamEvent> events;
    if (type == QStringLiteral("error"))
    {
        auto error = providerError(object.value("error"));
        events.push_back(AiStreamEvent{.type = AiStreamEventType::responseFailed, .error = std::move(error)});
    }
    else if (type == QStringLiteral("message_start"))
    {
        const auto message = object.value("message").toObject();
        m_responseId = utf8(message.value("id").toString());
        if (!m_started)
        {
            m_started = true;
            events.push_back(AiStreamEvent{.type = AiStreamEventType::responseStarted, .responseId = m_responseId});
        }
        const auto usage = message.value("usage").toObject();
        if (!usage.isEmpty())
        {
            events.push_back(AiStreamEvent{
                .type = AiStreamEventType::usageUpdated,
                .responseId = m_responseId,
                .usage = AiTokenUsage{.inputTokens = usage.value("input_tokens").toVariant().toULongLong(),
                                      .cachedInputTokens =
                                          usage.value("cache_read_input_tokens").toVariant().toULongLong()}});
        }
    }
    else if (type == QStringLiteral("content_block_start"))
    {
        const auto index = static_cast<std::size_t>(object.value("index").toInt());
        const auto block = object.value("content_block").toObject();
        if (block.value("type").toString() == QStringLiteral("tool_use"))
        {
            ToolState state{.callId = utf8(block.value("id").toString()), .name = utf8(block.value("name").toString())};
            m_toolsByIndex.insert_or_assign(index, state);
            events.push_back(AiStreamEvent{.type = AiStreamEventType::toolCallStarted,
                                           .responseId = m_responseId,
                                           .toolCallId = state.callId,
                                           .toolName = state.name});
        }
    }
    else if (type == QStringLiteral("content_block_delta"))
    {
        const auto index = static_cast<std::size_t>(object.value("index").toInt());
        const auto delta = object.value("delta").toObject();
        const auto deltaType = delta.value("type").toString();
        if (deltaType == QStringLiteral("text_delta"))
        {
            events.push_back(AiStreamEvent{.type = AiStreamEventType::textDelta,
                                           .responseId = m_responseId,
                                           .delta = utf8(delta.value("text").toString())});
        }
        else if (deltaType == QStringLiteral("thinking_delta"))
        {
            events.push_back(AiStreamEvent{.type = AiStreamEventType::reasoningDelta,
                                           .responseId = m_responseId,
                                           .delta = utf8(delta.value("thinking").toString())});
        }
        else if (deltaType == QStringLiteral("signature_delta"))
        {
            events.push_back(AiStreamEvent{.type = AiStreamEventType::reasoningSignatureDelta,
                                           .responseId = m_responseId,
                                           .delta = utf8(delta.value("signature").toString())});
        }
        else if (deltaType == QStringLiteral("input_json_delta"))
        {
            const auto tool = m_toolsByIndex.find(index);
            events.push_back(
                AiStreamEvent{.type = AiStreamEventType::toolArgumentsDelta,
                              .responseId = m_responseId,
                              .toolCallId = tool == m_toolsByIndex.end() ? std::string{} : tool->second.callId,
                              .toolName = tool == m_toolsByIndex.end() ? std::string{} : tool->second.name,
                              .delta = utf8(delta.value("partial_json").toString())});
        }
    }
    else if (type == QStringLiteral("content_block_stop"))
    {
        const auto index = static_cast<std::size_t>(object.value("index").toInt());
        const auto tool = m_toolsByIndex.find(index);
        if (tool != m_toolsByIndex.end())
        {
            events.push_back(AiStreamEvent{.type = AiStreamEventType::toolCallCompleted,
                                           .responseId = m_responseId,
                                           .toolCallId = tool->second.callId,
                                           .toolName = tool->second.name});
        }
    }
    else if (type == QStringLiteral("message_delta"))
    {
        const auto usage = object.value("usage").toObject();
        if (!usage.isEmpty())
        {
            events.push_back(AiStreamEvent{
                .type = AiStreamEventType::usageUpdated,
                .responseId = m_responseId,
                .usage = AiTokenUsage{.outputTokens = usage.value("output_tokens").toVariant().toULongLong()}});
        }
    }
    else if (type == QStringLiteral("message_stop") && !m_completed)
    {
        m_completed = true;
        events.push_back(AiStreamEvent{.type = AiStreamEventType::responseCompleted, .responseId = m_responseId});
    }
    return events;
}

void AnthropicStreamMapper::reset() noexcept
{
    m_toolsByIndex.clear();
    m_responseId.clear();
    m_started = false;
    m_completed = false;
}

std::expected<std::vector<AiStreamEvent>, AiProviderError> OllamaStreamMapper::map(const std::string_view line)
{
    const auto parsed = parseObject(line);
    if (!parsed.has_value())
    {
        return std::unexpected(parsed.error());
    }
    const auto &object = parsed.value();
    if (object.contains("error"))
    {
        AiProviderError error{.code = AiProviderErrorCode::server,
                              .message = utf8(object.value("error").toString()),
                              .retryable = false};
        return std::vector{AiStreamEvent{.type = AiStreamEventType::responseFailed, .error = std::move(error)}};
    }

    std::vector<AiStreamEvent> events;
    if (!m_started)
    {
        m_started = true;
        events.push_back(AiStreamEvent{.type = AiStreamEventType::responseStarted});
    }
    const auto message = object.value("message").toObject();
    auto content = utf8(message.value("content").toString());
    if (content.empty())
    {
        content = utf8(object.value("response").toString());
    }
    if (!content.empty())
    {
        events.push_back(AiStreamEvent{.type = AiStreamEventType::textDelta, .delta = std::move(content)});
    }
    auto thinking = utf8(message.value("thinking").toString());
    if (thinking.empty())
    {
        thinking = utf8(object.value("thinking").toString());
    }
    if (!thinking.empty())
    {
        events.push_back(AiStreamEvent{.type = AiStreamEventType::reasoningDelta, .delta = std::move(thinking)});
    }
    const auto toolCalls = message.value("tool_calls").toArray();
    for (const auto toolValue : toolCalls)
    {
        const auto function = toolValue.toObject().value("function").toObject();
        const auto callId = syntheticToolId(++m_toolSequence);
        const auto name = utf8(function.value("name").toString());
        const auto arguments = QJsonDocument(function.value("arguments").toObject()).toJson(QJsonDocument::Compact);
        events.push_back(
            AiStreamEvent{.type = AiStreamEventType::toolCallStarted, .toolCallId = callId, .toolName = name});
        events.push_back(AiStreamEvent{.type = AiStreamEventType::toolCallCompleted,
                                       .toolCallId = callId,
                                       .toolName = name,
                                       .delta = {arguments.constData(), static_cast<std::size_t>(arguments.size())}});
    }
    if (object.value("done").toBool() && !m_completed)
    {
        m_completed = true;
        const auto usage = AiTokenUsage{.inputTokens = object.value("prompt_eval_count").toVariant().toULongLong(),
                                        .outputTokens = object.value("eval_count").toVariant().toULongLong()};
        events.push_back(AiStreamEvent{.type = AiStreamEventType::usageUpdated, .usage = usage});
        events.push_back(AiStreamEvent{.type = AiStreamEventType::responseCompleted});
    }
    return events;
}

void OllamaStreamMapper::reset() noexcept
{
    m_toolSequence = 0;
    m_started = false;
    m_completed = false;
}

} // namespace ztermy::ai
