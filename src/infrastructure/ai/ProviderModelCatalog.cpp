#include "infrastructure/ai/ProviderModelCatalog.h"

#include "infrastructure/ai/OpenAiSubscriptionAuthProtocol.h"
#include "infrastructure/ai/ProviderEndpointResolver.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <limits>
#include <utility>

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

std::expected<QNetworkRequest, AiProviderError>
ProviderModelCatalog::prepareOpenAiSubscriptionRequest(security::SensitiveByteArray accessToken,
                                                       const QString &accountId, const QString &clientVersion)
{
    if (accessToken.empty() || accountId.trimmed().isEmpty())
    {
        accessToken.clear();
        return std::unexpected(AiProviderError{.code = AiProviderErrorCode::authentication,
                                               .message = "ChatGPT sign-in is incomplete.",
                                               .retryable = false});
    }
    QNetworkRequest request(OpenAiSubscriptionAuthProtocol::modelsUrl(clientVersion));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("ChatGPT-Account-Id", accountId.trimmed().toUtf8());
    request.setRawHeader("originator", "ztermy");
    request.setRawHeader("User-Agent", QByteArray("ztermy/") + clientVersion.trimmed().toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15'000);
    const auto token = accessToken.view();
    QByteArray authorization("Bearer ");
    authorization.append(token.data(), static_cast<qsizetype>(token.size()));
    request.setRawHeader("Authorization", authorization);
    authorization.fill('\0');
    accessToken.clear();
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

std::expected<QStringList, AiProviderError> ProviderModelCatalog::parseOpenAiSubscription(const QByteArray &body)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(AiProviderError{.code = AiProviderErrorCode::protocol,
                                               .message = "ChatGPT returned an invalid model list.",
                                               .retryable = false});
    }
    struct Entry final
    {
        QString slug;
        qint64 priority = std::numeric_limits<qint64>::max();
    };
    std::vector<Entry> entries;
    const QJsonArray values = document.object().value(QStringLiteral("models")).toArray();
    entries.reserve(static_cast<std::size_t>(values.size()));
    for (const QJsonValue &value : values)
    {
        const QJsonObject object = value.toObject();
        if (object.contains(QStringLiteral("supported_in_api"))
            && !object.value(QStringLiteral("supported_in_api")).toBool())
        {
            continue;
        }
        QString slug = object.value(QStringLiteral("slug")).toString().trimmed();
        if (slug.isEmpty())
        {
            slug = object.value(QStringLiteral("id")).toString().trimmed();
        }
        if (slug.isEmpty() || slug.size() > 256 || std::ranges::any_of(entries, [&slug](const Entry &entry) {
                return entry.slug == slug;
            }))
        {
            continue;
        }
        entries.push_back(
            {.slug = std::move(slug),
             .priority = object.value(QStringLiteral("priority")).toInteger(std::numeric_limits<qint64>::max())});
    }
    std::ranges::stable_sort(entries, [](const Entry &left, const Entry &right) {
        if (left.priority != right.priority)
        {
            return left.priority < right.priority;
        }
        return QString::localeAwareCompare(left.slug, right.slug) < 0;
    });
    QStringList models;
    models.reserve(static_cast<qsizetype>(entries.size()));
    for (auto &entry : entries)
    {
        models.push_back(std::move(entry.slug));
    }
    return models;
}

} // namespace ztermy::ai
