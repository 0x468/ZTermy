#include "infrastructure/ai/ProviderRequestFactory.h"
#include "infrastructure/ai/ProviderEndpointResolver.h"

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
    if (configuration.flavor != AiProviderFlavor::kimi)
    {
        return;
    }

    const QString model = fromUtf8(configuration.model).toLower();
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
            function.insert(QStringLiteral("strict"), true);
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
            calls.append(QJsonObject{
                {QStringLiteral("id"), fromUtf8(call.id)},
                {QStringLiteral("type"), QStringLiteral("function")},
                {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), fromUtf8(call.name)},
                                                         {QStringLiteral("arguments"), serializedArguments}}}});
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

[[nodiscard]] QJsonArray responsesInput(const AiGenerationRequest &generation)
{
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
                     {QStringLiteral("input"), responsesInput(generation)},
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
    if (!previousResponseId.empty())
    {
        body.insert(QStringLiteral("previous_response_id"), fromUtf8(previousResponseId));
    }
    QJsonObject reasoning{{QStringLiteral("summary"), QStringLiteral("auto")}};
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
    body.insert(QStringLiteral("reasoning"), reasoning);
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

[[nodiscard]] QJsonArray anthropicMessages(const AiGenerationRequest &generation)
{
    QJsonArray result;
    for (const auto &message : generation.messages)
    {
        if (message.role == AiMessageRole::system || message.role == AiMessageRole::tool)
        {
            continue;
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
        result.append(
            QJsonObject{{QStringLiteral("role"), roleName(message.role)}, {QStringLiteral("content"), content}});
    }
    for (const auto &exchange : generation.toolHistory)
    {
        QJsonArray uses;
        if (!exchange.providerAssistantContentJson.empty())
        {
            QJsonParseError parseError;
            const auto content = QJsonDocument::fromJson(
                QByteArray(exchange.providerAssistantContentJson.data(),
                           static_cast<qsizetype>(exchange.providerAssistantContentJson.size())),
                &parseError);
            if (parseError.error == QJsonParseError::NoError && content.isArray())
            {
                uses = content.array();
            }
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
            result.append(
                QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), uses}});
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
            result.append(
                QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), outputs}});
        }
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
    QJsonObject body{{QStringLiteral("model"), fromUtf8(configuration.model)},
                     {QStringLiteral("messages"), anthropicMessages(generation)},
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
    if (const auto imageError = validateImages(generation); imageError.has_value())
    {
        return std::unexpected(*imageError);
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
    return PreparedProviderRequest{.request = request,
                                   .body = QJsonDocument(body).toJson(QJsonDocument::Compact),
                                   .protocol = protocol};
}

} // namespace ztermy::ai
