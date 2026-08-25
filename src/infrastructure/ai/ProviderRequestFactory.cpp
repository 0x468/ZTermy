#include "infrastructure/ai/ProviderRequestFactory.h"
#include "infrastructure/ai/ProviderEndpointResolver.h"

#include "domain/ai/AiProviderReplayCodec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QUrl>

#include <array>
#include <optional>

namespace ztermy::ai
{
namespace
{

[[nodiscard]] QString roleName(const AiMessageRole role)
{
    switch (role)
    {
        case AiMessageRole::system:
            return QStringLiteral("system");
        case AiMessageRole::user:
            return QStringLiteral("user");
        case AiMessageRole::assistant:
            return QStringLiteral("assistant");
        case AiMessageRole::tool:
            return QStringLiteral("tool");
    }
    return QStringLiteral("user");
}

[[nodiscard]] QString fromUtf8(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::expected<QUrl, AiProviderError> endpointUrl(const AiProviderConfiguration &configuration)
{
    return resolveProviderEndpoint(configuration, ProviderEndpointPurpose::generation);
}

[[nodiscard]] QString reasoningEffortToken(const AiReasoningEffort effort, const bool xhighForMaximum = false)
{
    switch (effort)
    {
        case AiReasoningEffort::low:
            return QStringLiteral("low");
        case AiReasoningEffort::medium:
            return QStringLiteral("medium");
        case AiReasoningEffort::high:
            return QStringLiteral("high");
        case AiReasoningEffort::maximum:
            return xhighForMaximum ? QStringLiteral("xhigh") : QStringLiteral("max");
        case AiReasoningEffort::automatic:
        case AiReasoningEffort::disabled:
            return {};
    }
    return {};
}

void applyCompatibleReasoning(QJsonObject &body, const AiProviderConfiguration &configuration,
                              const AiReasoningEffort effort)
{
    const QString model = fromUtf8(configuration.model).toLower();
    if (configuration.flavor == AiProviderFlavor::qwen && model.startsWith(QStringLiteral("qwen3.8")))
    {
        // Qwen 3.8 enables preserved thinking by default, which requires every
        // previous reasoning_content value to be replayed exactly. ztermy keeps
        // visible bounded reasoning, not an authoritative provider transcript,
        // so opt out instead of sending an incomplete reasoning chain.
        body.insert(QStringLiteral("preserve_thinking"), false);
    }
    if (effort == AiReasoningEffort::automatic)
    {
        return;
    }
    if (configuration.flavor == AiProviderFlavor::deepSeek)
    {
        body.insert(
            QStringLiteral("thinking"),
            QJsonObject{{QStringLiteral("type"), effort == AiReasoningEffort::disabled ? QStringLiteral("disabled")
                                                                                       : QStringLiteral("enabled")}});
        if (effort != AiReasoningEffort::automatic && effort != AiReasoningEffort::disabled)
        {
            const auto mapped = effort == AiReasoningEffort::medium ? AiReasoningEffort::high : effort;
            body.insert(QStringLiteral("reasoning_effort"), reasoningEffortToken(mapped));
        }
        return;
    }
    if (configuration.flavor == AiProviderFlavor::zai)
    {
        body.insert(
            QStringLiteral("thinking"),
            QJsonObject{{QStringLiteral("type"), effort == AiReasoningEffort::disabled ? QStringLiteral("disabled")
                                                                                       : QStringLiteral("enabled")},
                        {QStringLiteral("clear_thinking"), false}});
        return;
    }
    if (configuration.flavor == AiProviderFlavor::gemini)
    {
        const QString effortToken = reasoningEffortToken(effort);
        if (!effortToken.isEmpty())
        {
            body.insert(QStringLiteral("reasoning_effort"), effortToken);
        }
        return;
    }
    if (configuration.flavor == AiProviderFlavor::openRouter)
    {
        body.insert(QStringLiteral("reasoning_effort"),
                    effort == AiReasoningEffort::disabled ? QStringLiteral("none") : reasoningEffortToken(effort));
        return;
    }
    if (configuration.flavor == AiProviderFlavor::qwen)
    {
        body.insert(QStringLiteral("enable_thinking"), effort != AiReasoningEffort::disabled);
        return;
    }
    if (configuration.flavor != AiProviderFlavor::kimi)
    {
        return;
    }

    if (model.startsWith(QStringLiteral("kimi-k3")))
    {
        if (effort != AiReasoningEffort::automatic && effort != AiReasoningEffort::disabled)
        {
            const auto mapped = effort == AiReasoningEffort::medium ? AiReasoningEffort::high : effort;
            body.insert(QStringLiteral("reasoning_effort"), reasoningEffortToken(mapped));
        }
    }
    else if (model.startsWith(QStringLiteral("kimi-k2.5")) || model.startsWith(QStringLiteral("kimi-k2.6")))
    {
        body.insert(
            QStringLiteral("thinking"),
            QJsonObject{{QStringLiteral("type"), effort == AiReasoningEffort::disabled ? QStringLiteral("disabled")
                                                                                       : QStringLiteral("enabled")},
                        {QStringLiteral("keep"), QStringLiteral("all")}});
    }
}

[[nodiscard]] QString imageDataUrl(const AiImageAttachment &image)
{
    return QStringLiteral("data:%1;base64,%2").arg(fromUtf8(image.mediaType), fromUtf8(image.base64Data));
}

[[nodiscard]] QJsonObject compatibleMessage(const AiChatMessage &message, const bool ollamaFormat)
{
    QJsonObject object{{QStringLiteral("role"), roleName(message.role)},
                       {QStringLiteral("content"), fromUtf8(message.content)}};
    if (message.role == AiMessageRole::tool && !message.toolCallId.empty())
    {
        object.insert(QStringLiteral("tool_call_id"), fromUtf8(message.toolCallId));
    }
    if (message.images.empty())
    {
        return object;
    }
    if (ollamaFormat)
    {
        QJsonArray images;
        for (const auto &image : message.images)
        {
            images.append(fromUtf8(image.base64Data));
        }
        object.insert(QStringLiteral("images"), images);
        return object;
    }

    QJsonArray content;
    if (!message.content.empty())
    {
        content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                   {QStringLiteral("text"), fromUtf8(message.content)}});
    }
    for (const auto &image : message.images)
    {
        content.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("image_url")},
            {QStringLiteral("image_url"), QJsonObject{{QStringLiteral("url"), imageDataUrl(image)},
                                                      {QStringLiteral("detail"), QStringLiteral("auto")}}}});
    }
    object.insert(QStringLiteral("content"), content);
    return object;
}

[[nodiscard]] QJsonArray messages(const AiGenerationRequest &generation, const bool ollamaFormat = false)
{
    QJsonArray result;
    for (const auto &message : generation.messages)
    {
        result.append(compatibleMessage(message, ollamaFormat));
    }
    return result;
}

[[nodiscard]] QJsonObject responsesMessage(const AiChatMessage &message)
{
    if (message.images.empty())
    {
        return QJsonObject{{QStringLiteral("role"), roleName(message.role)},
                           {QStringLiteral("content"), fromUtf8(message.content)}};
    }
    QJsonArray content;
    if (!message.content.empty())
    {
        content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("input_text")},
                                   {QStringLiteral("text"), fromUtf8(message.content)}});
    }
    for (const auto &image : message.images)
    {
        content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("input_image")},
                                   {QStringLiteral("image_url"), imageDataUrl(image)},
                                   {QStringLiteral("detail"), QStringLiteral("auto")}});
    }
    return QJsonObject{{QStringLiteral("role"), roleName(message.role)}, {QStringLiteral("content"), content}};
}

[[nodiscard]] std::optional<AiProviderError> validateImages(const AiGenerationRequest &generation)
{
    constexpr std::size_t maximumImagesPerMessage = 4;
    constexpr std::size_t maximumBase64Bytes = std::size_t{16} * 1024 * 1024;
    for (const auto &message : generation.messages)
    {
        if (message.images.size() > maximumImagesPerMessage
            || (!message.images.empty() && message.role != AiMessageRole::user))
        {
            return AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                                   .message = "Only user messages may contain up to four image attachments.",
                                   .retryable = false};
        }
        for (const auto &image : message.images)
        {
            const bool supported = image.mediaType == "image/png" || image.mediaType == "image/jpeg"
                                   || image.mediaType == "image/webp" || image.mediaType == "image/gif";
            if (!supported || image.base64Data.empty() || image.base64Data.size() > maximumBase64Bytes)
            {
                return AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                                       .message = "An image attachment has an unsupported type or size.",
                                       .retryable = false};
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<AiProviderError> validateCompatibleToolHistory(const AiGenerationRequest &generation)
{
    constexpr std::size_t maximumProviderDataBytes = std::size_t{64} * 1024;
    for (const auto &exchange : generation.toolHistory)
    {
        for (const auto &call : exchange.calls)
        {
            QJsonParseError argumentsError;
            const QJsonDocument arguments = QJsonDocument::fromJson(
                QByteArray(call.argumentsJson.data(), static_cast<qsizetype>(call.argumentsJson.size())),
                &argumentsError);
            if (argumentsError.error != QJsonParseError::NoError || !arguments.isObject())
            {
                return AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                                       .message = "AI tool history contains invalid JSON arguments.",
                                       .retryable = false};
            }
            if (call.providerDataJson.empty())
            {
                continue;
            }
            if (call.providerDataJson.size() > maximumProviderDataBytes)
            {
                return AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                                       .message = "Provider tool metadata exceeds the 64 KiB limit.",
                                       .retryable = false};
            }
            QJsonParseError providerDataError;
            const QJsonDocument providerData = QJsonDocument::fromJson(
                QByteArray(call.providerDataJson.data(), static_cast<qsizetype>(call.providerDataJson.size())),
                &providerDataError);
            if (providerDataError.error != QJsonParseError::NoError || !providerData.isObject())
            {
                return AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                                       .message = "Provider tool metadata must be a JSON object.",
                                       .retryable = false};
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool schemaTypeContains(const QJsonValue &type, const QString &expected)
{
    if (type.isString())
    {
        return type.toString() == expected;
    }
    return type.isArray() && type.toArray().contains(expected);
}

[[nodiscard]] bool jsonSchemaSupportsStrict(const QJsonObject &schema, const bool root = false)
{
    if (schema.contains(QStringLiteral("$ref")))
    {
        return false;
    }
    for (const auto *keyword : {"anyOf", "oneOf", "allOf"})
    {
        const QString key = QString::fromLatin1(keyword);
        if (!schema.contains(key))
        {
            continue;
        }
        const QJsonArray alternatives = schema.value(key).toArray();
        if (alternatives.isEmpty() || std::ranges::any_of(alternatives, [](const QJsonValue &alternative) {
                return !alternative.isObject() || !jsonSchemaSupportsStrict(alternative.toObject());
            }))
        {
            return false;
        }
    }

    const QJsonValue type = schema.value(QStringLiteral("type"));
    const bool objectType = schemaTypeContains(type, QStringLiteral("object"));
    const bool arrayType = schemaTypeContains(type, QStringLiteral("array"));
    if (root && !objectType)
    {
        return false;
    }
    if (objectType)
    {
        const QJsonValue propertiesValue = schema.value(QStringLiteral("properties"));
        const QJsonValue requiredValue = schema.value(QStringLiteral("required"));
        if (!propertiesValue.isObject() || !requiredValue.isArray()
            || schema.value(QStringLiteral("additionalProperties")) != QJsonValue(false))
        {
            return false;
        }
        const QJsonObject properties = propertiesValue.toObject();
        const QJsonArray required = requiredValue.toArray();
        if (required.size() != properties.size())
        {
            return false;
        }
        for (auto property = properties.constBegin(); property != properties.constEnd(); ++property)
        {
            if (!required.contains(property.key()) || !property.value().isObject()
                || !jsonSchemaSupportsStrict(property.value().toObject()))
            {
                return false;
            }
        }
    }
    if (arrayType)
    {
        const QJsonValue items = schema.value(QStringLiteral("items"));
        if (!items.isObject() || !jsonSchemaSupportsStrict(items.toObject()))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::expected<QJsonArray, AiProviderError> toolDefinitions(const AiGenerationRequest &generation,
                                                                         const bool responsesFormat)
{
    QJsonArray result;
    for (const auto &definition : generation.tools)
    {
        QJsonParseError parseError;
        const auto parameters = QJsonDocument::fromJson(
            QByteArray(definition.parametersJson.data(), static_cast<qsizetype>(definition.parametersJson.size())),
            &parseError);
        if (definition.name.empty() || !parameters.isObject() || parseError.error != QJsonParseError::NoError)
        {
            return std::unexpected(
                AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                                .message = "AI tool definitions must have a name and JSON object schema.",
                                .retryable = false});
        }
        QJsonObject function{{QStringLiteral("name"), fromUtf8(definition.name)},
                             {QStringLiteral("description"), fromUtf8(definition.description)},
                             {QStringLiteral("parameters"), parameters.object()}};
        if (responsesFormat)
        {
            function.insert(QStringLiteral("type"), QStringLiteral("function"));
            function.insert(QStringLiteral("strict"), jsonSchemaSupportsStrict(parameters.object(), true));
            result.append(function);
        }
        else
        {
            result.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                                      {QStringLiteral("function"), function}});
        }
    }
    return result;
}

[[nodiscard]] QJsonArray conversationMessages(const AiGenerationRequest &generation, const bool objectArguments,
                                              const QString &reasoningKey = {}, const bool ollamaFormat = false)
{
    auto result = messages(generation, ollamaFormat);
    for (const auto &exchange : generation.toolHistory)
    {
        QJsonArray calls;
        for (const auto &call : exchange.calls)
        {
            QJsonParseError parseError;
            const auto arguments = QJsonDocument::fromJson(
                QByteArray(call.argumentsJson.data(), static_cast<qsizetype>(call.argumentsJson.size())), &parseError);
            const auto argumentsObject = arguments.isObject() && parseError.error == QJsonParseError::NoError
                                             ? arguments.object()
                                             : QJsonObject{};
            const QJsonValue serializedArguments =
                objectArguments
                    ? QJsonValue{argumentsObject}
                    : QJsonValue{QString::fromUtf8(QJsonDocument(argumentsObject).toJson(QJsonDocument::Compact))};
            QJsonObject serializedCall{
                {QStringLiteral("id"), fromUtf8(call.id)},
                {QStringLiteral("type"), QStringLiteral("function")},
                {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), fromUtf8(call.name)},
                                                         {QStringLiteral("arguments"), serializedArguments}}}};
            if (!call.providerDataJson.empty())
            {
                QJsonParseError providerDataError;
                const QJsonDocument providerData = QJsonDocument::fromJson(
                    QByteArray(call.providerDataJson.data(), static_cast<qsizetype>(call.providerDataJson.size())),
                    &providerDataError);
                if (providerDataError.error == QJsonParseError::NoError && providerData.isObject())
                {
                    serializedCall.insert(QStringLiteral("extra_content"), providerData.object());
                }
            }
            calls.append(serializedCall);
        }
        if (!calls.isEmpty())
        {
            QJsonObject assistant{{QStringLiteral("role"), QStringLiteral("assistant")},
                                  {QStringLiteral("content"), QJsonValue::Null},
                                  {QStringLiteral("tool_calls"), calls}};
            if (!reasoningKey.isEmpty() && !exchange.reasoning.empty())
            {
                assistant.insert(reasoningKey, fromUtf8(exchange.reasoning));
            }
            result.append(assistant);
        }
        for (const auto &output : exchange.outputs)
        {
            result.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")},
                                      {QStringLiteral("tool_call_id"), fromUtf8(output.callId)},
                                      {QStringLiteral("name"), fromUtf8(output.name)},
                                      {QStringLiteral("content"), fromUtf8(output.outputJson)}});
        }
    }
    return result;
}

[[nodiscard]] QJsonArray responsesInput(const AiGenerationRequest &generation, const bool replayToolHistory)
{
    if (replayToolHistory)
    {
        QJsonArray result;
        for (const auto &message : generation.messages)
        {
            result.append(responsesMessage(message));
        }
        for (const auto &exchange : generation.toolHistory)
        {
            for (const auto &call : exchange.calls)
            {
                result.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function_call")},
                                          {QStringLiteral("call_id"), fromUtf8(call.id)},
                                          {QStringLiteral("name"), fromUtf8(call.name)},
                                          {QStringLiteral("arguments"), fromUtf8(call.argumentsJson)}});
            }
            for (const auto &output : exchange.outputs)
            {
                result.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function_call_output")},
                                          {QStringLiteral("call_id"), fromUtf8(output.callId)},
                                          {QStringLiteral("output"), fromUtf8(output.outputJson)}});
            }
        }
        return result;
    }
    if (generation.toolHistory.empty() || generation.toolHistory.back().outputs.empty())
    {
        QJsonArray result;
        for (const auto &message : generation.messages)
        {
            result.append(responsesMessage(message));
        }
        return result;
    }
    QJsonArray result;
    for (const auto &output : generation.toolHistory.back().outputs)
    {
        result.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function_call_output")},
                                  {QStringLiteral("call_id"), fromUtf8(output.callId)},
                                  {QStringLiteral("output"), fromUtf8(output.outputJson)}});
    }
    return result;
}

[[nodiscard]] QJsonObject openAiBody(const AiProviderConfiguration &configuration,
                                     const AiGenerationRequest &generation, const QJsonArray &tools)
{
    QJsonArray effectiveTools = tools;
    if (generation.webSearchEnabled)
    {
        effectiveTools.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("web_search")}});
    }
    QJsonObject body{{QStringLiteral("model"), fromUtf8(configuration.model)},
                     {QStringLiteral("input"), responsesInput(generation, configuration.chatGptSubscription)},
                     {QStringLiteral("stream"), true},
                     {QStringLiteral("store"), false}};
    if (!generation.instructions.empty())
    {
        body.insert(QStringLiteral("instructions"), fromUtf8(generation.instructions));
    }
    if (!effectiveTools.isEmpty())
    {
        body.insert(QStringLiteral("tools"), effectiveTools);
    }
    const auto previousResponseId = generation.previousResponseId.value_or(std::string{});
    if (!configuration.chatGptSubscription && !previousResponseId.empty())
    {
        body.insert(QStringLiteral("previous_response_id"), fromUtf8(previousResponseId));
    }
    QJsonObject reasoning;
    if (configuration.supportsReasoningSummaryParameter)
    {
        reasoning.insert(QStringLiteral("summary"), QStringLiteral("auto"));
    }
    if (generation.reasoningEffort == AiReasoningEffort::disabled)
    {
        reasoning.insert(QStringLiteral("effort"), QStringLiteral("none"));
    }
    else
    {
        const QString effort = reasoningEffortToken(generation.reasoningEffort, true);
        if (!effort.isEmpty())
        {
            reasoning.insert(QStringLiteral("effort"), effort);
        }
    }
    if (!reasoning.isEmpty())
    {
        body.insert(QStringLiteral("reasoning"), reasoning);
    }
    return body;
}

[[nodiscard]] QJsonObject compatibleBody(const AiProviderConfiguration &configuration,
                                         const AiGenerationRequest &generation, const QJsonArray &tools)
{
    auto input = conversationMessages(generation, false, QStringLiteral("reasoning_content"));
    if (!generation.instructions.empty())
    {
        input.prepend(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                  {QStringLiteral("content"), fromUtf8(generation.instructions)}});
    }
    QJsonObject body{{QStringLiteral("model"), fromUtf8(configuration.model)},
                     {QStringLiteral("messages"), input},
                     {QStringLiteral("stream"), true},
                     {QStringLiteral("stream_options"), QJsonObject{{QStringLiteral("include_usage"), true}}}};
    if (!tools.isEmpty())
    {
        body.insert(QStringLiteral("tools"), tools);
    }
    applyCompatibleReasoning(body, configuration, generation.reasoningEffort);
    return body;
}

[[nodiscard]] QJsonArray anthropicContentBlocks(const QJsonValue &content)
{
    if (content.isArray())
    {
        return content.toArray();
    }
    return {
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), content.toString()}}};
}

void appendAnthropicMessage(QJsonArray &messages, const QString &role, const QJsonValue &content)
{
    if (!messages.isEmpty())
    {
        QJsonObject previous = messages.last().toObject();
        if (previous.value(QStringLiteral("role")).toString() == role)
        {
            QJsonArray merged = anthropicContentBlocks(previous.value(QStringLiteral("content")));
            const QJsonArray appended = anthropicContentBlocks(content);
            for (const auto &value : appended)
            {
                merged.append(value);
            }
            previous.insert(QStringLiteral("content"), merged);
            messages[messages.size() - 1] = previous;
            return;
        }
    }
    messages.append(QJsonObject{{QStringLiteral("role"), role}, {QStringLiteral("content"), content}});
}

[[nodiscard]] std::expected<void, AiProviderError>
appendAnthropicExchanges(QJsonArray &result, const std::span<const AiToolExchange> toolHistory)
{
    for (const auto &exchange : toolHistory)
    {
        QJsonArray uses;
        if (!exchange.providerAssistantContentJson.empty())
        {
            QJsonParseError parseError;
            const auto content = QJsonDocument::fromJson(
                QByteArray(exchange.providerAssistantContentJson.data(),
                           static_cast<qsizetype>(exchange.providerAssistantContentJson.size())),
                &parseError);
            if (parseError.error != QJsonParseError::NoError || !content.isArray())
            {
                return std::unexpected(AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                                                       .message = "Anthropic replay content is invalid.",
                                                       .retryable = false});
            }
            uses = content.array();
        }
        else if (!exchange.reasoning.empty() && !exchange.reasoningSignature.empty())
        {
            uses.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("thinking")},
                                    {QStringLiteral("thinking"), fromUtf8(exchange.reasoning)},
                                    {QStringLiteral("signature"), fromUtf8(exchange.reasoningSignature)}});
        }
        if (exchange.providerAssistantContentJson.empty())
        {
            for (const auto &call : exchange.calls)
            {
                QJsonParseError parseError;
                const auto arguments = QJsonDocument::fromJson(
                    QByteArray(call.argumentsJson.data(), static_cast<qsizetype>(call.argumentsJson.size())),
                    &parseError);
                uses.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_use")},
                                        {QStringLiteral("id"), fromUtf8(call.id)},
                                        {QStringLiteral("name"), fromUtf8(call.name)},
                                        {QStringLiteral("input"), arguments.isObject() ? QJsonValue(arguments.object())
                                                                                       : QJsonValue(QJsonObject{})}});
            }
        }
        if (!uses.isEmpty())
        {
            appendAnthropicMessage(result, QStringLiteral("assistant"), uses);
        }
        QJsonArray outputs;
        for (const auto &output : exchange.outputs)
        {
            outputs.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_result")},
                                       {QStringLiteral("tool_use_id"), fromUtf8(output.callId)},
                                       {QStringLiteral("content"), fromUtf8(output.outputJson)}});
        }
        if (!outputs.isEmpty())
        {
            appendAnthropicMessage(result, QStringLiteral("user"), outputs);
        }
    }
    return {};
}

[[nodiscard]] std::expected<QJsonArray, AiProviderError> anthropicMessages(const AiGenerationRequest &generation)
{
    QJsonArray result;
    for (const auto &message : generation.messages)
    {
        if (message.role == AiMessageRole::system || message.role == AiMessageRole::tool)
        {
            continue;
        }
        if (!message.providerReplayJson.empty())
        {
            if (message.role != AiMessageRole::assistant)
            {
                return std::unexpected(AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                                                       .message = "Provider replay belongs to an assistant message.",
                                                       .retryable = false});
            }
            const auto replay = AiProviderReplayCodec::decode(message.providerReplayJson);
            if (!replay.has_value())
            {
                return std::unexpected(AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                                                       .message = "Provider replay content is invalid.",
                                                       .retryable = false});
            }
            const auto appended = appendAnthropicExchanges(result, replay->toolHistory);
            if (!appended.has_value())
            {
                return std::unexpected(appended.error());
            }
            if (!replay->finalAssistantContentJson.empty())
            {
                const auto finalContent = QJsonDocument::fromJson(
                    QByteArray(replay->finalAssistantContentJson.data(),
                               static_cast<qsizetype>(replay->finalAssistantContentJson.size())));
                appendAnthropicMessage(result, QStringLiteral("assistant"), finalContent.array());
                continue;
            }
        }

        QJsonValue content = fromUtf8(message.content);
        if (!message.images.empty())
        {
            QJsonArray parts;
            if (!message.content.empty())
            {
                parts.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                         {QStringLiteral("text"), fromUtf8(message.content)}});
            }
            for (const auto &image : message.images)
            {
                parts.append(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("image")},
                    {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("base64")},
                                                           {QStringLiteral("media_type"), fromUtf8(image.mediaType)},
                                                           {QStringLiteral("data"), fromUtf8(image.base64Data)}}}});
            }
            content = parts;
        }
        appendAnthropicMessage(result, roleName(message.role), content);
    }
    const auto appended = appendAnthropicExchanges(result, generation.toolHistory);
    if (!appended.has_value())
    {
        return std::unexpected(appended.error());
    }
    return result;
}

[[nodiscard]] std::expected<QJsonObject, AiProviderError> anthropicBody(const AiProviderConfiguration &configuration,
                                                                        const AiGenerationRequest &generation)
{
    QJsonArray tools;
    for (const auto &definition : generation.tools)
    {
        QJsonParseError parseError;
        const auto parameters = QJsonDocument::fromJson(
            QByteArray(definition.parametersJson.data(), static_cast<qsizetype>(definition.parametersJson.size())),
            &parseError);
        if (definition.name.empty() || !parameters.isObject() || parseError.error != QJsonParseError::NoError)
        {
            return std::unexpected(
                AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                                .message = "AI tool definitions must have a name and JSON object schema.",
                                .retryable = false});
        }
        tools.append(QJsonObject{{QStringLiteral("name"), fromUtf8(definition.name)},
                                 {QStringLiteral("description"), fromUtf8(definition.description)},
                                 {QStringLiteral("input_schema"), parameters.object()}});
    }
    if (generation.webSearchEnabled)
    {
        tools.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("web_search_20250305")},
                                 {QStringLiteral("name"), QStringLiteral("web_search")},
                                 {QStringLiteral("max_uses"), 5}});
    }
    const auto messages = anthropicMessages(generation);
    if (!messages.has_value())
    {
        return std::unexpected(messages.error());
    }
    QJsonObject body{{QStringLiteral("model"), fromUtf8(configuration.model)},
                     {QStringLiteral("messages"), *messages},
                     {QStringLiteral("max_tokens"), 8192},
                     {QStringLiteral("stream"), true}};
    if (!generation.instructions.empty())
    {
        body.insert(QStringLiteral("system"), fromUtf8(generation.instructions));
    }
    if (!tools.isEmpty())
    {
        body.insert(QStringLiteral("tools"), tools);
    }
    const QString effort = reasoningEffortToken(generation.reasoningEffort);
    if (!effort.isEmpty())
    {
        body.insert(QStringLiteral("thinking"), QJsonObject{{QStringLiteral("type"), QStringLiteral("adaptive")}});
        body.insert(QStringLiteral("output_config"), QJsonObject{{QStringLiteral("effort"), effort}});
    }
    return body;
}

[[nodiscard]] QJsonObject ollamaBody(const AiProviderConfiguration &configuration,
                                     const AiGenerationRequest &generation, const QJsonArray &tools)
{
    auto input = conversationMessages(generation, true, QStringLiteral("thinking"), true);
    if (!generation.instructions.empty())
    {
        input.prepend(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                  {QStringLiteral("content"), fromUtf8(generation.instructions)}});
    }
    QJsonObject body{{QStringLiteral("model"), fromUtf8(configuration.model)},
                     {QStringLiteral("messages"), input},
                     {QStringLiteral("stream"), true}};
    if (!tools.isEmpty())
    {
        body.insert(QStringLiteral("tools"), tools);
    }
    return body;
}

} // namespace

std::expected<PreparedProviderRequest, AiProviderError>
ProviderRequestFactory::prepare(const AiProviderConfiguration &configuration, const AiGenerationRequest &generation,
                                const std::string_view apiKey)
{
    if (configuration.model.empty())
    {
        return std::unexpected(AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                                               .message = "Provider model is required.",
                                               .retryable = false});
    }
    if (configuration.chatGptSubscription && configuration.accountId.empty())
    {
        return std::unexpected(AiProviderError{.code = AiProviderErrorCode::authentication,
                                               .message = "The ChatGPT account identity is unavailable.",
                                               .retryable = false});
    }
    if (const auto imageError = validateImages(generation); imageError.has_value())
    {
        return std::unexpected(*imageError);
    }
    if (configuration.kind == AiProviderKind::openAiCompatible)
    {
        if (const auto toolHistoryError = validateCompatibleToolHistory(generation); toolHistoryError.has_value())
        {
            return std::unexpected(*toolHistoryError);
        }
    }
    if (generation.webSearchEnabled && configuration.kind != AiProviderKind::openAiResponses
        && configuration.kind != AiProviderKind::anthropicMessages)
    {
        return std::unexpected(AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                                               .message = "This provider protocol has no native web search tool.",
                                               .retryable = false});
    }
    const auto url = endpointUrl(configuration);
    if (!url.has_value())
    {
        return std::unexpected(url.error());
    }

    QJsonObject body;
    auto protocol = AiWireProtocol::serverSentEvents;
    const auto tools = toolDefinitions(generation, configuration.kind == AiProviderKind::openAiResponses);
    if (!tools.has_value())
    {
        return std::unexpected(tools.error());
    }
    switch (configuration.kind)
    {
        case AiProviderKind::openAiResponses:
            body = openAiBody(configuration, generation, *tools);
            break;
        case AiProviderKind::anthropicMessages:
        {
            const auto anthropic = anthropicBody(configuration, generation);
            if (!anthropic.has_value())
            {
                return std::unexpected(anthropic.error());
            }
            body = *anthropic;
            break;
        }
        case AiProviderKind::ollama:
            body = ollamaBody(configuration, generation, *tools);
            protocol = AiWireProtocol::ndjson;
            break;
        case AiProviderKind::openAiCompatible:
            body = compatibleBody(configuration, generation, *tools);
            break;
    }

    QNetworkRequest request(url.value());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept",
                         protocol == AiWireProtocol::serverSentEvents ? "text/event-stream" : "application/x-ndjson");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(120'000);
    if (!apiKey.empty() && configuration.kind == AiProviderKind::anthropicMessages)
    {
        request.setRawHeader("x-api-key", QByteArray(apiKey.data(), static_cast<qsizetype>(apiKey.size())));
        request.setRawHeader("anthropic-version", "2023-06-01");
    }
    else if (!apiKey.empty())
    {
        QByteArray authorization("Bearer ");
        authorization.append(apiKey.data(), static_cast<qsizetype>(apiKey.size()));
        request.setRawHeader("Authorization", authorization);
        authorization.fill('\0');
    }
    if (configuration.chatGptSubscription)
    {
        request.setRawHeader("ChatGPT-Account-Id", fromUtf8(configuration.accountId).toUtf8());
        request.setRawHeader("originator", "ztermy");
        request.setRawHeader("User-Agent", "ztermy/0.4.2");
    }
    return PreparedProviderRequest{.request = request,
                                   .body = QJsonDocument(body).toJson(QJsonDocument::Compact),
                                   .protocol = protocol};
}

} // namespace ztermy::ai
