#pragma once

#include "core/security/SensitiveByteArray.h"
#include "domain/ai/AiProviderTypes.h"

#include <QByteArray>
#include <QNetworkRequest>
#include <QString>

#include <expected>
#include <optional>
#include <vector>

namespace ztermy::ai
{

struct OpenAiSubscriptionUsageWindow final
{
    double usedPercent = 0.0;
    qint64 durationSeconds = 0;
    qint64 resetAtUtcSeconds = 0;

    [[nodiscard]] friend bool operator==(const OpenAiSubscriptionUsageWindow &,
                                         const OpenAiSubscriptionUsageWindow &) = default;
};

struct OpenAiSubscriptionUsageLimit final
{
    QString name;
    QString meteredFeature;
    bool allowed = true;
    bool reached = false;
    std::optional<OpenAiSubscriptionUsageWindow> primary;
    std::optional<OpenAiSubscriptionUsageWindow> secondary;

    [[nodiscard]] friend bool operator==(const OpenAiSubscriptionUsageLimit &,
                                         const OpenAiSubscriptionUsageLimit &) = default;
};

struct OpenAiSubscriptionUsageSnapshot final
{
    QString planType;
    OpenAiSubscriptionUsageLimit codex;
    std::vector<OpenAiSubscriptionUsageLimit> additional;
    bool hasCredits = false;
    bool unlimitedCredits = false;
    QString creditBalance;

    [[nodiscard]] friend bool operator==(const OpenAiSubscriptionUsageSnapshot &,
                                         const OpenAiSubscriptionUsageSnapshot &) = default;
};

class OpenAiSubscriptionUsage final
{
public:
    [[nodiscard]] static std::expected<QNetworkRequest, AiProviderError>
    prepareRequest(security::SensitiveByteArray accessToken, const QString &accountId, const QString &clientVersion);
    [[nodiscard]] static std::expected<OpenAiSubscriptionUsageSnapshot, AiProviderError> parse(const QByteArray &body);
};

} // namespace ztermy::ai
