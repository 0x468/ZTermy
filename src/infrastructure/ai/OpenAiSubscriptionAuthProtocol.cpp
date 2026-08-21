#include "infrastructure/ai/OpenAiSubscriptionAuthProtocol.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QUrlQuery>

#include <algorithm>
#include <cstring>
#include <limits>

namespace
{

constexpr auto ClientId = "app_EMoamEEZ73f0CkXaXp7hrann";
constexpr auto Issuer = "https://auth.openai.com";
constexpr auto InferenceEndpoint = "https://chatgpt.com/backend-api/codex/responses";

[[nodiscard]] QByteArray base64Url(const QByteArrayView bytes)
{
    return QByteArray(bytes.data(), bytes.size())
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

[[nodiscard]] QByteArray randomBytes(const qsizetype count)
{
    QByteArray bytes(count, Qt::Uninitialized);
    QRandomGenerator *generator = QRandomGenerator::system();
    for (qsizetype offset = 0; offset < count; offset += static_cast<qsizetype>(sizeof(quint32)))
    {
        const quint32 value = generator->generate();
        const qsizetype remaining = std::min<qsizetype>(static_cast<qsizetype>(sizeof(value)), count - offset);
        std::memcpy(bytes.data() + offset, &value, static_cast<std::size_t>(remaining));
    }
    return bytes;
}

[[nodiscard]] QString accountIdFromToken(const QByteArrayView token)
{
    const QList<QByteArray> segments = QByteArray(token.data(), token.size()).split('.');
    if (segments.size() != 3)
    {
        return {};
    }
    const QByteArray payload = QByteArray::fromBase64(segments.at(1), QByteArray::Base64UrlEncoding);
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject())
    {
        return {};
    }
    const QJsonObject claims = document.object();
    QString accountId = claims.value(QStringLiteral("chatgpt_account_id")).toString();
    if (!accountId.isEmpty())
    {
        return accountId;
    }
    accountId = claims.value(QStringLiteral("https://api.openai.com/auth"))
                    .toObject()
                    .value(QStringLiteral("chatgpt_account_id"))
                    .toString();
    if (!accountId.isEmpty())
    {
        return accountId;
    }
    const QJsonArray organizations = claims.value(QStringLiteral("organizations")).toArray();
    return organizations.isEmpty() ? QString{}
                                   : organizations.first().toObject().value(QStringLiteral("id")).toString();
}

} // namespace

namespace ztermy::ai
{

OpenAiSubscriptionPkce OpenAiSubscriptionAuthProtocol::createPkce()
{
    const QByteArray verifier = base64Url(randomBytes(32));
    return {.verifier = verifier, .challenge = challengeForVerifier(verifier), .state = base64Url(randomBytes(32))};
}

QByteArray OpenAiSubscriptionAuthProtocol::challengeForVerifier(const QByteArrayView verifier)
{
    return base64Url(
        QCryptographicHash::hash(QByteArray(verifier.data(), verifier.size()), QCryptographicHash::Sha256));
}

QUrl OpenAiSubscriptionAuthProtocol::authorizationUrl(const OpenAiSubscriptionPkce &pkce, const QUrl &redirectUri,
                                                      const QUrl &authorizationEndpoint)
{
    if (pkce.verifier.size() < 43 || pkce.challenge.isEmpty() || pkce.state.isEmpty() || !redirectUri.isValid())
    {
        return {};
    }
    QUrl url = authorizationEndpoint.isEmpty() ? issuerUrl().resolved(QUrl(QStringLiteral("/oauth/authorize")))
                                               : authorizationEndpoint;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(ClientId));
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri.toString());
    query.addQueryItem(QStringLiteral("scope"), QStringLiteral("openid profile email offline_access"));
    query.addQueryItem(QStringLiteral("code_challenge"), QString::fromLatin1(pkce.challenge));
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("id_token_add_organizations"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("codex_cli_simplified_flow"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("state"), QString::fromLatin1(pkce.state));
    query.addQueryItem(QStringLiteral("originator"), QStringLiteral("ztermy"));
    url.setQuery(query);
    return url;
}

QByteArray OpenAiSubscriptionAuthProtocol::authorizationCodeForm(const QByteArrayView code,
                                                                 const QByteArrayView verifier, const QUrl &redirectUri)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    query.addQueryItem(QStringLiteral("code"), QString::fromLatin1(code));
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri.toString());
    query.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(ClientId));
    query.addQueryItem(QStringLiteral("code_verifier"), QString::fromLatin1(verifier));
    return query.query(QUrl::FullyEncoded).toUtf8();
}

QByteArray OpenAiSubscriptionAuthProtocol::refreshTokenForm(const QByteArrayView refreshToken)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    query.addQueryItem(QStringLiteral("refresh_token"), QString::fromLatin1(refreshToken));
    query.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(ClientId));
    return query.query(QUrl::FullyEncoded).toUtf8();
}

std::expected<OpenAiSubscriptionTokenResponse, OpenAiSubscriptionAuthError>
OpenAiSubscriptionAuthProtocol::parseTokenResponse(const QByteArrayView body, const bool requireRefreshToken)
{
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(body.data(), body.size()));
    if (!document.isObject())
    {
        return std::unexpected(OpenAiSubscriptionAuthError::invalidResponse);
    }
    const QJsonObject object = document.object();
    QByteArray accessToken = object.value(QStringLiteral("access_token")).toString().toUtf8();
    QByteArray refreshToken = object.value(QStringLiteral("refresh_token")).toString().toUtf8();
    const QByteArray idToken = object.value(QStringLiteral("id_token")).toString().toUtf8();
    if (accessToken.isEmpty())
    {
        return std::unexpected(OpenAiSubscriptionAuthError::missingAccessToken);
    }
    if (requireRefreshToken && refreshToken.isEmpty())
    {
        return std::unexpected(OpenAiSubscriptionAuthError::missingRefreshToken);
    }
    const qint64 expires = object.value(QStringLiteral("expires_in")).toInteger(3600);
    if (expires <= 0 || expires > std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected(OpenAiSubscriptionAuthError::invalidResponse);
    }
    QString accountId = accountIdFromToken(idToken);
    if (accountId.isEmpty())
    {
        accountId = accountIdFromToken(accessToken);
    }
    return OpenAiSubscriptionTokenResponse{
        .accessToken = security::SensitiveByteArray(std::move(accessToken)),
        .refreshToken = security::SensitiveByteArray(std::move(refreshToken)),
        .accountId = std::move(accountId),
        .expiresInSeconds = static_cast<std::uint32_t>(expires),
    };
}

QUrl OpenAiSubscriptionAuthProtocol::issuerUrl()
{
    return QUrl(QString::fromLatin1(Issuer));
}

QUrl OpenAiSubscriptionAuthProtocol::tokenUrl()
{
    return issuerUrl().resolved(QUrl(QStringLiteral("/oauth/token")));
}

QUrl OpenAiSubscriptionAuthProtocol::inferenceUrl()
{
    return QUrl(QString::fromLatin1(InferenceEndpoint));
}

QByteArray OpenAiSubscriptionAuthProtocol::clientId()
{
    return QByteArray(ClientId);
}

} // namespace ztermy::ai
