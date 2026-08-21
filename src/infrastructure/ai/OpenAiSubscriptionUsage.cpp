#include "infrastructure/ai/OpenAiSubscriptionUsage.h"

#include "infrastructure/ai/OpenAiSubscriptionAuthProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace
{

using ztermy::ai::AiProviderError;
using ztermy::ai::AiProviderErrorCode;
using ztermy::ai::OpenAiSubscriptionUsageLimit;
using ztermy::ai::OpenAiSubscriptionUsageWindow;

[[nodiscard]] AiProviderError protocolError()
{
    return {.code = AiProviderErrorCode::protocol,
            .message = "ChatGPT returned invalid subscription usage data.",
            .retryable = false};
}

[[nodiscard]] std::expected<std::optional<OpenAiSubscriptionUsageWindow>, AiProviderError>
parseWindow(const QJsonValue &value)
{
    if (value.isNull() || value.isUndefined())
    {
        return std::optional<OpenAiSubscriptionUsageWindow>{};
    }
    if (!value.isObject())
    {
        return std::unexpected(protocolError());
    }
    const QJsonObject object = value.toObject();
    const double usedPercent = object.value(QStringLiteral("used_percent")).toDouble(-1.0);
    const qint64 duration = object.value(QStringLiteral("limit_window_seconds")).toInteger(0);
    const qint64 resetAt = object.value(QStringLiteral("reset_at")).toInteger(0);
    if (!std::isfinite(usedPercent) || usedPercent < 0.0 || usedPercent > 100.0 || duration < 0 || resetAt < 0)
    {
        return std::unexpected(protocolError());
    }
    return std::optional{OpenAiSubscriptionUsageWindow{.usedPercent = usedPercent,
                                                       .durationSeconds = duration,
                                                       .resetAtUtcSeconds = resetAt}};
}

[[nodiscard]] std::expected<OpenAiSubscriptionUsageLimit, AiProviderError>
parseLimit(const QJsonObject &object, QString name = {}, QString meteredFeature = {})
{
    auto primary = parseWindow(object.value(QStringLiteral("primary_window")));
    auto secondary = parseWindow(object.value(QStringLiteral("secondary_window")));
    if (!primary.has_value() || !secondary.has_value())
    {
        return std::unexpected(protocolError());
    }
    return OpenAiSubscriptionUsageLimit{
        .name = std::move(name),
        .meteredFeature = std::move(meteredFeature),
        .allowed = !object.contains(QStringLiteral("allowed")) || object.value(QStringLiteral("allowed")).toBool(),
        .reached = object.value(QStringLiteral("limit_reached")).toBool(),
        .primary = std::move(*primary),
        .secondary = std::move(*secondary),
    };
}

} // namespace

namespace ztermy::ai
{

std::expected<QNetworkRequest, AiProviderError>
OpenAiSubscriptionUsage::prepareRequest(security::SensitiveByteArray accessToken, const QString &accountId,
                                        const QString &clientVersion)
{
    if (accessToken.empty() || accountId.trimmed().isEmpty())
    {
        accessToken.clear();
        return std::unexpected(AiProviderError{.code = AiProviderErrorCode::authentication,
                                               .message = "ChatGPT sign-in is incomplete.",
                                               .retryable = false});
    }
    QNetworkRequest request(OpenAiSubscriptionAuthProtocol::usageUrl());
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("ChatGPT-Account-Id", accountId.trimmed().toUtf8());
    request.setRawHeader("originator", "ztermy");
    request.setRawHeader("User-Agent", QByteArray("ztermy/") + clientVersion.trimmed().toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15'000);
    const std::string_view token = accessToken.view();
    QByteArray authorization("Bearer ");
    authorization.append(token.data(), static_cast<qsizetype>(token.size()));
    request.setRawHeader("Authorization", authorization);
    authorization.fill('\0');
    accessToken.clear();
    return request;
}

std::expected<OpenAiSubscriptionUsageSnapshot, AiProviderError> OpenAiSubscriptionUsage::parse(const QByteArray &body)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(protocolError());
    }
    const QJsonObject root = document.object();
    const QJsonValue rateLimitValue = root.value(QStringLiteral("rate_limit"));
    if (!rateLimitValue.isObject())
    {
        return std::unexpected(protocolError());
    }
    auto codex = parseLimit(rateLimitValue.toObject(), QStringLiteral("Codex"), QStringLiteral("codex"));
    if (!codex.has_value())
    {
        return std::unexpected(codex.error());
    }

    OpenAiSubscriptionUsageSnapshot snapshot{.planType = root.value(QStringLiteral("plan_type")).toString().trimmed(),
                                             .codex = std::move(*codex)};
    if (snapshot.planType.size() > 64)
    {
        return std::unexpected(protocolError());
    }
    const QJsonArray additional = root.value(QStringLiteral("additional_rate_limits")).toArray();
    snapshot.additional.reserve(static_cast<std::size_t>(std::min(additional.size(), qsizetype{16})));
    for (const QJsonValue &value : additional)
    {
        if (snapshot.additional.size() >= 16 || !value.isObject())
        {
            break;
        }
        const QJsonObject entry = value.toObject();
        QString name = entry.value(QStringLiteral("limit_name")).toString().trimmed();
        QString feature = entry.value(QStringLiteral("metered_feature")).toString().trimmed();
        if (name.size() > 128 || feature.size() > 128 || !entry.value(QStringLiteral("rate_limit")).isObject())
        {
            continue;
        }
        auto parsed =
            parseLimit(entry.value(QStringLiteral("rate_limit")).toObject(), std::move(name), std::move(feature));
        if (parsed.has_value())
        {
            snapshot.additional.push_back(std::move(*parsed));
        }
    }
    const QJsonObject credits = root.value(QStringLiteral("credits")).toObject();
    snapshot.hasCredits = credits.value(QStringLiteral("has_credits")).toBool();
    snapshot.unlimitedCredits = credits.value(QStringLiteral("unlimited")).toBool();
    snapshot.creditBalance = credits.value(QStringLiteral("balance")).toVariant().toString().trimmed();
    if (snapshot.creditBalance.size() > 128)
    {
        snapshot.creditBalance.truncate(128);
    }
    return snapshot;
}

} // namespace ztermy::ai
