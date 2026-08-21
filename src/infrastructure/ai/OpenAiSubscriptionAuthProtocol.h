#pragma once

#include "core/security/SensitiveByteArray.h"

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <expected>

namespace ztermy::ai
{

struct OpenAiSubscriptionPkce final
{
    QByteArray verifier;
    QByteArray challenge;
    QByteArray state;
};

struct OpenAiSubscriptionTokenResponse final
{
    security::SensitiveByteArray accessToken;
    security::SensitiveByteArray refreshToken;
    QString accountId;
    std::uint32_t expiresInSeconds = 3600;
};

enum class OpenAiSubscriptionAuthError : std::uint8_t
{
    invalidInput,
    invalidResponse,
    missingAccessToken,
    missingRefreshToken,
};

class OpenAiSubscriptionAuthProtocol final
{
public:
    [[nodiscard]] static OpenAiSubscriptionPkce createPkce();
    [[nodiscard]] static QByteArray challengeForVerifier(QByteArrayView verifier);
    [[nodiscard]] static QUrl authorizationUrl(const OpenAiSubscriptionPkce &pkce, const QUrl &redirectUri);
    [[nodiscard]] static QByteArray authorizationCodeForm(QByteArrayView code, QByteArrayView verifier,
                                                          const QUrl &redirectUri);
    [[nodiscard]] static QByteArray refreshTokenForm(QByteArrayView refreshToken);
    [[nodiscard]] static std::expected<OpenAiSubscriptionTokenResponse, OpenAiSubscriptionAuthError>
    parseTokenResponse(QByteArrayView body, bool requireRefreshToken);

    [[nodiscard]] static QUrl issuerUrl();
    [[nodiscard]] static QUrl tokenUrl();
    [[nodiscard]] static QUrl inferenceUrl();
    [[nodiscard]] static QByteArray clientId();
};

} // namespace ztermy::ai
