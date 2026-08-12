#include "infrastructure/ai/ProviderModelCatalog.h"

#include "infrastructure/ai/ProviderEndpointResolver.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>

namespace ztermy::ai
{

std::expected<QNetworkRequest, AiProviderError>
ProviderModelCatalog::prepareRequest(const AiProviderConfiguration &configuration, security::SensitiveByteArray apiKey)
{
    const auto endpoint = resolveProviderEndpoint(configuration, ProviderEndpointPurpose::models);
    if (!endpoint.has_value())
    {
        apiKey.clear();
        return std::unexpected(endpoint.error());
    }
    QNetworkRequest request(*endpoint);
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15'000);
    if (!apiKey.empty() && configuration.kind == AiProviderKind::anthropicMessages)
    {
        const auto key = apiKey.view();
        request.setRawHeader("x-api-key", QByteArray(key.data(), static_cast<qsizetype>(key.size())));
        request.setRawHeader("anthropic-version", "2023-06-01");
    }
    else if (!apiKey.empty() && configuration.kind != AiProviderKind::ollama)
    {
        const auto key = apiKey.view();
        QByteArray authorization("Bearer ");
        authorization.append(key.data(), static_cast<qsizetype>(key.size()));
        request.setRawHeader("Authorization", authorization);
        authorization.fill('\0');
    }
    apiKey.clear();
    return request;
}

std::expected<QStringList, AiProviderError> ProviderModelCatalog::parse(const AiProviderKind kind,
                                                                        const QByteArray &body)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(AiProviderError{.code = AiProviderErrorCode::protocol,
                                               .message = "The provider returned an invalid model list.",
                                               .retryable = false});
    }
    const auto root = document.object();
    const auto entries = kind == AiProviderKind::ollama ? root.value("models").toArray() : root.value("data").toArray();
    QStringList models;
    models.reserve(entries.size());
    for (const auto &entry : entries)
    {
        const auto object = entry.toObject();
        auto id = kind == AiProviderKind::ollama ? object.value("model").toString() : object.value("id").toString();
        if (id.isEmpty() && kind == AiProviderKind::ollama)
        {
            id = object.value("name").toString();
        }
        id = id.trimmed();
        if (!id.isEmpty() && id.size() <= 256 && !models.contains(id))
        {
            models.push_back(id);
        }
    }
    std::ranges::sort(models, [](const QString &left, const QString &right) {
        return QString::localeAwareCompare(left, right) < 0;
    });
    return models;
}

} // namespace ztermy::ai
