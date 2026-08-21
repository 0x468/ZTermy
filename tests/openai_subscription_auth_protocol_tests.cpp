#include "infrastructure/ai/OpenAiSubscriptionAuthProtocol.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QUrlQuery>

#include <string_view>

namespace
{

[[nodiscard]] QByteArray bytes(const ztermy::security::SensitiveByteArray &secret)
{
    const std::string_view view = secret.view();
    return {view.data(), static_cast<qsizetype>(view.size())};
}

[[nodiscard]] QByteArray jwtWithAccount(const QString &accountId)
{
    const QByteArray header =
        QByteArrayLiteral(R"({"alg":"none"})").toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QByteArray payload =
        QJsonDocument(QJsonObject{{QStringLiteral("https://api.openai.com/auth"),
                                   QJsonObject{{QStringLiteral("chatgpt_account_id"), accountId}}}})
            .toJson(QJsonDocument::Compact)
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return header + '.' + payload + '.';
}

class OpenAiSubscriptionAuthProtocolTests final : public QObject
{
    Q_OBJECT

private slots:
    void derivesRfc7636Challenge();
    void createsBoundedPkceAndAuthorizationUrl();
    void createsEncodedTokenForms();
    void parsesTokensAndAccountIdentity();
    void rejectsIncompleteTokenResponses();
};

void OpenAiSubscriptionAuthProtocolTests::derivesRfc7636Challenge()
{
    QCOMPARE(ztermy::ai::OpenAiSubscriptionAuthProtocol::challengeForVerifier(
                 QByteArrayLiteral("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk")),
             QByteArrayLiteral("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
}

void OpenAiSubscriptionAuthProtocolTests::createsBoundedPkceAndAuthorizationUrl()
{
    const auto pkce = ztermy::ai::OpenAiSubscriptionAuthProtocol::createPkce();
    QCOMPARE(pkce.verifier.size(), 43);
    QCOMPARE(pkce.challenge.size(), 43);
    QCOMPARE(pkce.state.size(), 43);
    QVERIFY(pkce.verifier != pkce.state);

    const QUrl redirect(QStringLiteral("http://127.0.0.1:1455/auth/callback"));
    const QUrl url = ztermy::ai::OpenAiSubscriptionAuthProtocol::authorizationUrl(pkce, redirect);
    QCOMPARE(url.scheme(), QStringLiteral("https"));
    QCOMPARE(url.host(), QStringLiteral("auth.openai.com"));
    QCOMPARE(url.path(), QStringLiteral("/oauth/authorize"));
    const QUrlQuery query(url);
    QCOMPARE(query.queryItemValue(QStringLiteral("client_id")),
             QString::fromLatin1(ztermy::ai::OpenAiSubscriptionAuthProtocol::clientId()));
    QCOMPARE(query.queryItemValue(QStringLiteral("redirect_uri")), redirect.toString());
    QCOMPARE(query.queryItemValue(QStringLiteral("code_challenge")), QString::fromLatin1(pkce.challenge));
    QCOMPARE(query.queryItemValue(QStringLiteral("state")), QString::fromLatin1(pkce.state));
    QCOMPARE(query.queryItemValue(QStringLiteral("originator")), QStringLiteral("ztermy"));
}

void OpenAiSubscriptionAuthProtocolTests::createsEncodedTokenForms()
{
    const QByteArray codeForm = ztermy::ai::OpenAiSubscriptionAuthProtocol::authorizationCodeForm(
        QByteArrayLiteral("code+/="), QByteArrayLiteral("verifier~value"),
        QUrl(QStringLiteral("http://127.0.0.1:1455/auth/callback")));
    const QUrlQuery codeQuery(QString::fromUtf8(codeForm));
    QCOMPARE(codeQuery.queryItemValue(QStringLiteral("grant_type")), QStringLiteral("authorization_code"));
    QCOMPARE(codeQuery.queryItemValue(QStringLiteral("code")), QStringLiteral("code+/="));
    QCOMPARE(codeQuery.queryItemValue(QStringLiteral("code_verifier")), QStringLiteral("verifier~value"));

    const QUrlQuery refreshQuery(QString::fromUtf8(
        ztermy::ai::OpenAiSubscriptionAuthProtocol::refreshTokenForm(QByteArrayLiteral("refresh+/="))));
    QCOMPARE(refreshQuery.queryItemValue(QStringLiteral("grant_type")), QStringLiteral("refresh_token"));
    QCOMPARE(refreshQuery.queryItemValue(QStringLiteral("refresh_token")), QStringLiteral("refresh+/="));
}

void OpenAiSubscriptionAuthProtocolTests::parsesTokensAndAccountIdentity()
{
    const QByteArray body =
        QJsonDocument(QJsonObject{{QStringLiteral("access_token"), QStringLiteral("access")},
                                  {QStringLiteral("refresh_token"), QStringLiteral("refresh")},
                                  {QStringLiteral("id_token"), QString::fromLatin1(jwtWithAccount("account-1"))},
                                  {QStringLiteral("expires_in"), 7200}})
            .toJson(QJsonDocument::Compact);
    auto parsed = ztermy::ai::OpenAiSubscriptionAuthProtocol::parseTokenResponse(body, true);
    QVERIFY(parsed);
    QCOMPARE(bytes(parsed->accessToken), QByteArrayLiteral("access"));
    QCOMPARE(bytes(parsed->refreshToken), QByteArrayLiteral("refresh"));
    QCOMPARE(parsed->accountId, QStringLiteral("account-1"));
    QCOMPARE(parsed->expiresInSeconds, std::uint32_t{7200});
}

void OpenAiSubscriptionAuthProtocolTests::rejectsIncompleteTokenResponses()
{
    auto parsed = ztermy::ai::OpenAiSubscriptionAuthProtocol::parseTokenResponse(QByteArrayLiteral("{}"), true);
    QVERIFY(!parsed);
    QCOMPARE(parsed.error(), ztermy::ai::OpenAiSubscriptionAuthError::missingAccessToken);

    parsed = ztermy::ai::OpenAiSubscriptionAuthProtocol::parseTokenResponse(
        QByteArrayLiteral(R"({"access_token":"access"})"), true);
    QVERIFY(!parsed);
    QCOMPARE(parsed.error(), ztermy::ai::OpenAiSubscriptionAuthError::missingRefreshToken);

    parsed = ztermy::ai::OpenAiSubscriptionAuthProtocol::parseTokenResponse(
        QByteArrayLiteral(R"({"access_token":"access"})"), false);
    QVERIFY(parsed);
    QVERIFY(parsed->refreshToken.empty());
}

} // namespace

QTEST_GUILESS_MAIN(OpenAiSubscriptionAuthProtocolTests)

#include "openai_subscription_auth_protocol_tests.moc"
