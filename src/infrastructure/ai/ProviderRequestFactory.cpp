#include "infrastructure/ai/ProviderRequestFactory.h"

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

[[nodiscard]] std::string_view defaultEndpoint(const AiProviderKind kind)
{
    switch (kind)
    {
        case AiProviderKind::openAiResponses:
            return "/responses";
        case AiProviderKind::ollama:
            return "/api/chat";
        case AiProviderKind::openAiCompatible:
            return "/chat/completions";
    }
    return {};
}

[[nodiscard]] std::expected<QUrl, AiProviderError> endpointUrl(const AiProviderConfiguration &configuration)
{
    auto base = QUrl(fromUtf8(configuration.baseUrl));
    if (!base.isValid() || base.host().isEmpty() || !base.userInfo().isEmpty()
        || (base.scheme() != QStringLiteral("http") && base.scheme() != QStringLiteral("https")))
    {
        return std::unexpected(
            AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                            .message = "Provider base URL must be an HTTP(S) URL without credentials.",
                            .retryable = false});
    }

    auto basePath = base.path();
    while (basePath.endsWith('/'))
    {
        basePath.chop(1);
    }
    auto endpoint = configuration.endpointPath.empty() ? fromUtf8(defaultEndpoint(configuration.kind))
                                                       : fromUtf8(configuration.endpointPath);
    if (!endpoint.startsWith('/'))
    {
        endpoint.prepend('/');
    }
    base.setPath(basePath + endpoint);
    base.setQuery(QString{});
    base.setFragment(QString{});
    return base;
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

[[nodiscard]] QJsonObject openAiBody(const AiProviderConfiguration &configuration,
                                     const AiGenerationRequest &generation)
{
    QJsonObject body{{QStringLiteral("model"), fromUtf8(configuration.model)},
                     {QStringLiteral("input"), messages(generation)},
                     {QStringLiteral("stream"), true},
                     {QStringLiteral("store"), false}};
    if (!generation.instructions.empty())
    {
        body.insert(QStringLiteral("instructions"), fromUtf8(generation.instructions));
    }
    const auto previousResponseId = generation.previousResponseId.value_or(std::string{});
    if (!previousResponseId.empty())
    {
        body.insert(QStringLiteral("previous_response_id"), fromUtf8(previousResponseId));
    }
    return body;
}

[[nodiscard]] QJsonObject compatibleBody(const AiProviderConfiguration &configuration,
                                         const AiGenerationRequest &generation)
{
    auto input = messages(generation);
    if (!generation.instructions.empty())
    {
        input.prepend(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                  {QStringLiteral("content"), fromUtf8(generation.instructions)}});
    }
    return QJsonObject{{QStringLiteral("model"), fromUtf8(configuration.model)},
                       {QStringLiteral("messages"), input},
                       {QStringLiteral("stream"), true},
                       {QStringLiteral("stream_options"), QJsonObject{{QStringLiteral("include_usage"), true}}}};
}

[[nodiscard]] QJsonObject ollamaBody(const AiProviderConfiguration &configuration,
                                     const AiGenerationRequest &generation)
{
    auto input = messages(generation);
    if (!generation.instructions.empty())
    {
        input.prepend(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                  {QStringLiteral("content"), fromUtf8(generation.instructions)}});
    }
    return QJsonObject{{QStringLiteral("model"), fromUtf8(configuration.model)},
                       {QStringLiteral("messages"), input},
                       {QStringLiteral("stream"), true}};
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
    switch (configuration.kind)
    {
        case AiProviderKind::openAiResponses:
            body = openAiBody(configuration, generation);
            break;
        case AiProviderKind::ollama:
            body = ollamaBody(configuration, generation);
            protocol = AiWireProtocol::ndjson;
            break;
        case AiProviderKind::openAiCompatible:
            body = compatibleBody(configuration, generation);
            break;
    }

    QNetworkRequest request(url.value());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept",
                         protocol == AiWireProtocol::serverSentEvents ? "text/event-stream" : "application/x-ndjson");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(120'000);
    if (!apiKey.empty())
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
