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

[[nodiscard]] QJsonArray messages(const AiGenerationRequest &generation)
{
    QJsonArray result;
    for (const auto &message : generation.messages)
    {
        QJsonObject object{{QStringLiteral("role"), roleName(message.role)},
                           {QStringLiteral("content"), fromUtf8(message.content)}};
        if (message.role == AiMessageRole::tool && !message.toolCallId.empty())
        {
            object.insert(QStringLiteral("tool_call_id"), fromUtf8(message.toolCallId));
        }
        result.append(object);
    }
    return result;
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
                                              const QString &reasoningKey = {})
{
    auto result = messages(generation);
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
        return messages(generation);
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
    QJsonObject body{{QStringLiteral("model"), fromUtf8(configuration.model)},
                     {QStringLiteral("input"), responsesInput(generation)},
                     {QStringLiteral("stream"), true},
                     {QStringLiteral("store"), false}};
    if (!generation.instructions.empty())
    {
        body.insert(QStringLiteral("instructions"), fromUtf8(generation.instructions));
    }
    if (!tools.isEmpty())
    {
        body.insert(QStringLiteral("tools"), tools);
    }
    const auto previousResponseId = generation.previousResponseId.value_or(std::string{});
    if (!previousResponseId.empty())
    {
        body.insert(QStringLiteral("previous_response_id"), fromUtf8(previousResponseId));
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
        result.append(QJsonObject{{QStringLiteral("role"), roleName(message.role)},
                                  {QStringLiteral("content"), fromUtf8(message.content)}});
    }
    for (const auto &exchange : generation.toolHistory)
    {
        QJsonArray uses;
        for (const auto &call : exchange.calls)
        {
            QJsonParseError parseError;
            const auto arguments = QJsonDocument::fromJson(
                QByteArray(call.argumentsJson.data(), static_cast<qsizetype>(call.argumentsJson.size())), &parseError);
            uses.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_use")},
                                    {QStringLiteral("id"), fromUtf8(call.id)},
                                    {QStringLiteral("name"), fromUtf8(call.name)},
                                    {QStringLiteral("input"), arguments.isObject() ? QJsonValue(arguments.object())
                                                                                   : QJsonValue(QJsonObject{})}});
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
    return body;
}

[[nodiscard]] QJsonObject ollamaBody(const AiProviderConfiguration &configuration,
                                     const AiGenerationRequest &generation, const QJsonArray &tools)
{
    auto input = conversationMessages(generation, true, QStringLiteral("thinking"));
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
