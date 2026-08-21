#include "infrastructure/ai/OpenAiSubscriptionAuthProtocol.h"
#include "infrastructure/ai/OpenAiSubscriptionAuthSession.h"
#include "infrastructure/ai/OpenAiSubscriptionTokenRefresher.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

#include <optional>
#include <string_view>

namespace
{

[[nodiscard]] QByteArray bytes(const ztermy::security::SensitiveByteArray &secret)
{
    const std::string_view view = secret.view();
    return {view.data(), static_cast<qsizetype>(view.size())};
}

[[nodiscard]] QByteArray jwtWithAccount(const QString &accountId, const qint64 expiration = 2'000'000'000)
{
    const QByteArray header =
        QByteArrayLiteral(R"({"alg":"none"})").toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QByteArray payload =
        QJsonDocument(QJsonObject{{QStringLiteral("https://api.openai.com/auth"),
                                   QJsonObject{{QStringLiteral("chatgpt_account_id"), accountId}}},
                                  {QStringLiteral("exp"), expiration}})
            .toJson(QJsonDocument::Compact)
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return header + '.' + payload + '.';
}

class FakeTokenServer final : public QObject
{
public:
    explicit FakeTokenServer(const bool includeRefreshToken = true) : m_includeRefreshToken(includeRefreshToken)
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *socket = m_server.nextPendingConnection();
            socket->setParent(this);
            QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                m_request += socket->readAll();
                const qsizetype headerEnd = m_request.indexOf(QByteArrayLiteral("\r\n\r\n"));
                if (headerEnd < 0)
                {
                    return;
                }
                QJsonObject response{
                    {QStringLiteral("access_token"), QStringLiteral("access")},
                    {QStringLiteral("id_token"), QString::fromLatin1(jwtWithAccount(QStringLiteral("account-1")))},
                    {QStringLiteral("expires_in"), 3600}};
                if (m_includeRefreshToken)
                {
                    response.insert(QStringLiteral("refresh_token"), QStringLiteral("refresh"));
                }
                const QByteArray body = QJsonDocument(response).toJson(QJsonDocument::Compact);
                socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: "
                                                "close\r\nContent-Length: ")
                              + QByteArray::number(body.size()) + QByteArrayLiteral("\r\n\r\n") + body);
                socket->disconnectFromHost();
            });
        });
        m_server.listen(QHostAddress::LocalHost, 0);
    }

    [[nodiscard]] QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/oauth/token").arg(m_server.serverPort()));
    }

    [[nodiscard]] const QByteArray &request() const noexcept { return m_request; }

private:
    QTcpServer m_server;
    QByteArray m_request;
    bool m_includeRefreshToken = true;
};

class OpenAiSubscriptionAuthProtocolTests final : public QObject
{
    Q_OBJECT

private slots:
    void derivesRfc7636Challenge();
    void createsBoundedPkceAndAuthorizationUrl();
    void createsEncodedTokenForms();
    void parsesTokensAndAccountIdentity();
    void readsPersistedAccessTokenClaims();
    void rejectsIncompleteTokenResponses();
    void browserSessionCompletesThroughLoopback();
    void browserSessionRejectsMismatchedState();
    void browserSessionCanBeCancelled();
    void refreshesAndRetainsUnrotatedRefreshToken();
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

void OpenAiSubscriptionAuthProtocolTests::readsPersistedAccessTokenClaims()
{
    const QByteArray token = jwtWithAccount(QStringLiteral("account-2"), 2'100'000'000);
    QCOMPARE(ztermy::ai::OpenAiSubscriptionAuthProtocol::accountIdFromAccessToken(token), QStringLiteral("account-2"));
    QCOMPARE(ztermy::ai::OpenAiSubscriptionAuthProtocol::expirationUtcSeconds(token),
             std::optional<qint64>{2'100'000'000});
    QVERIFY(
        !ztermy::ai::OpenAiSubscriptionAuthProtocol::expirationUtcSeconds(QByteArrayLiteral("not-a-jwt")).has_value());

    const QUrl models = ztermy::ai::OpenAiSubscriptionAuthProtocol::modelsUrl(QStringLiteral("0.3.0"));
    QCOMPARE(models.toString(), QStringLiteral("https://chatgpt.com/backend-api/codex/models?client_version=0.3.0"));
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

void OpenAiSubscriptionAuthProtocolTests::browserSessionCompletesThroughLoopback()
{
    FakeTokenServer tokenServer;
    QNetworkAccessManager network;
    ztermy::ai::OpenAiSubscriptionAuthSession session(&network);
    std::optional<ztermy::ai::OpenAiSubscriptionAuthSession::Result> result;
    auto started = session.beginBrowserLogin(
        [&result](ztermy::ai::OpenAiSubscriptionAuthSession::Result completed) {
            result = std::move(completed);
        },
        {.authorization = QUrl(QStringLiteral("https://auth.example.test/authorize")), .token = tokenServer.url()}, 0);
    QVERIFY(started);
    const QUrlQuery authorization(*started);
    QUrl callback(authorization.queryItemValue(QStringLiteral("redirect_uri")));
    QUrlQuery callbackQuery;
    callbackQuery.addQueryItem(QStringLiteral("code"), QStringLiteral("code-1"));
    callbackQuery.addQueryItem(QStringLiteral("state"), authorization.queryItemValue(QStringLiteral("state")));
    callback.setQuery(callbackQuery);
    QNetworkReply *browserReply = network.get(QNetworkRequest(callback));

    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 3000);
    QVERIFY(result->has_value());
    QCOMPARE(bytes(result->value().accessToken), QByteArrayLiteral("access"));
    QCOMPARE(bytes(result->value().refreshToken), QByteArrayLiteral("refresh"));
    QCOMPARE(result->value().accountId, QStringLiteral("account-1"));
    QTRY_COMPARE_WITH_TIMEOUT(browserReply->error(), QNetworkReply::NoError, 1000);
    QVERIFY(tokenServer.request().contains(QByteArrayLiteral("grant_type=authorization_code")));
    QVERIFY(!session.active());
    browserReply->deleteLater();
}

void OpenAiSubscriptionAuthProtocolTests::browserSessionRejectsMismatchedState()
{
    FakeTokenServer tokenServer;
    QNetworkAccessManager network;
    ztermy::ai::OpenAiSubscriptionAuthSession session(&network);
    std::optional<ztermy::ai::OpenAiSubscriptionAuthSession::Result> result;
    auto started = session.beginBrowserLogin(
        [&result](ztermy::ai::OpenAiSubscriptionAuthSession::Result completed) {
            result = std::move(completed);
        },
        {.authorization = QUrl(QStringLiteral("https://auth.example.test/authorize")), .token = tokenServer.url()}, 0);
    QVERIFY(started);
    QUrl callback(QUrlQuery(*started).queryItemValue(QStringLiteral("redirect_uri")));
    QUrlQuery callbackQuery;
    callbackQuery.addQueryItem(QStringLiteral("code"), QStringLiteral("code-1"));
    callbackQuery.addQueryItem(QStringLiteral("state"), QStringLiteral("wrong-state"));
    callback.setQuery(callbackQuery);
    QNetworkReply *browserReply = network.get(QNetworkRequest(callback));

    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 2000);
    QVERIFY(!result->has_value());
    QCOMPARE(result->error().code, ztermy::ai::OpenAiSubscriptionSessionErrorCode::callbackRejected);
    QVERIFY(tokenServer.request().isEmpty());
    browserReply->deleteLater();
}

void OpenAiSubscriptionAuthProtocolTests::browserSessionCanBeCancelled()
{
    QNetworkAccessManager network;
    ztermy::ai::OpenAiSubscriptionAuthSession session(&network);
    std::optional<ztermy::ai::OpenAiSubscriptionAuthSession::Result> result;
    auto started = session.beginBrowserLogin(
        [&result](ztermy::ai::OpenAiSubscriptionAuthSession::Result completed) {
            result = std::move(completed);
        },
        {}, 0);
    QVERIFY(started);
    session.cancel();
    QVERIFY(result.has_value());
    QVERIFY(!result->has_value());
    QCOMPARE(result->error().code, ztermy::ai::OpenAiSubscriptionSessionErrorCode::cancelled);
    QVERIFY(!session.active());
}

void OpenAiSubscriptionAuthProtocolTests::refreshesAndRetainsUnrotatedRefreshToken()
{
    FakeTokenServer tokenServer(false);
    QNetworkAccessManager network;
    ztermy::ai::OpenAiSubscriptionTokenRefresher refresher(&network);
    std::optional<ztermy::ai::OpenAiSubscriptionTokenRefresher::Result> result;
    const auto started = refresher.start(
        ztermy::security::SensitiveByteArray(QByteArrayLiteral("old-refresh")),
        [&result](ztermy::ai::OpenAiSubscriptionTokenRefresher::Result completed) {
            result = std::move(completed);
        },
        tokenServer.url());
    QVERIFY(started.has_value());
    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 3000);
    QVERIFY(result->has_value());
    QCOMPARE(bytes(result->value().accessToken), QByteArrayLiteral("access"));
    QCOMPARE(bytes(result->value().refreshToken), QByteArrayLiteral("old-refresh"));
    QVERIFY(tokenServer.request().contains(QByteArrayLiteral("grant_type=refresh_token")));
    QVERIFY(tokenServer.request().contains(QByteArrayLiteral("refresh_token=old-refresh")));
    QVERIFY(!refresher.active());
}

} // namespace

QTEST_GUILESS_MAIN(OpenAiSubscriptionAuthProtocolTests)

#include "openai_subscription_auth_protocol_tests.moc"
