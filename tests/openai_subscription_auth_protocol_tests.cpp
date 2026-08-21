#include "infrastructure/ai/OpenAiSubscriptionAuthProtocol.h"
#include "infrastructure/ai/OpenAiSubscriptionAuthSession.h"
#include "infrastructure/ai/OpenAiSubscriptionTokenRefresher.h"
#include "infrastructure/ai/OpenAiSubscriptionUsage.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace
{

template <typename T>
[[nodiscard]] T &requiredOptionalValue(std::optional<T> &value)
{
    if (!value.has_value())
    {
        qFatal("Required test result is missing.");
    }
    return *value;
}

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
        return {QStringLiteral("http://127.0.0.1:%1/oauth/token").arg(m_server.serverPort())};
    }

    [[nodiscard]] const QByteArray &request() const noexcept { return m_request; }

private:
    QTcpServer m_server;
    QByteArray m_request;
    bool m_includeRefreshToken = true;
};

class FakeRefreshErrorServer final : public QObject
{
public:
    FakeRefreshErrorServer()
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *socket = m_server.nextPendingConnection();
            socket->setParent(this);
            QObject::connect(socket, &QTcpSocket::readyRead, this, [socket] {
                static_cast<void>(socket->readAll());
                const QByteArray body = QByteArrayLiteral(
                    R"({"error":{"message":"Token refresh is temporarily rate limited.","type":"rate_limit_exceeded"}})");
                socket->write(QByteArrayLiteral("HTTP/1.1 429 Too Many Requests\r\nContent-Type: application/json\r\n"
                                                "Retry-After: 3\r\nConnection: close\r\nContent-Length: ")
                              + QByteArray::number(body.size()) + QByteArrayLiteral("\r\n\r\n") + body);
                socket->disconnectFromHost();
            });
        });
        m_server.listen(QHostAddress::LocalHost, 0);
    }

    [[nodiscard]] QUrl url() const
    {
        return {QStringLiteral("http://127.0.0.1:%1/oauth/token").arg(m_server.serverPort())};
    }

private:
    QTcpServer m_server;
};

class FakeDeviceAuthServer final : public QObject
{
public:
    FakeDeviceAuthServer()
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *socket = m_server.nextPendingConnection();
            socket->setParent(this);
            auto request = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket, request] {
                request->append(socket->readAll());
                if (!request->contains(QByteArrayLiteral("\r\n\r\n")))
                {
                    return;
                }
                m_requests.push_back(*request);
                const bool userCodeRequest = request->startsWith(QByteArrayLiteral("POST /device/usercode "));
                const QByteArray body =
                    userCodeRequest
                        ? QByteArrayLiteral(R"({"device_auth_id":"device-1","user_code":"ABCD-1234","interval":"1"})")
                        : QByteArrayLiteral(
                              R"({"authorization_code":"code-1","code_challenge":"challenge-1","code_verifier":"verifier-1"})");
                socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: "
                                                "close\r\nContent-Length: ")
                              + QByteArray::number(body.size()) + QByteArrayLiteral("\r\n\r\n") + body);
                socket->disconnectFromHost();
            });
        });
        m_server.listen(QHostAddress::LocalHost, 0);
    }

    [[nodiscard]] QUrl userCodeUrl() const
    {
        return {QStringLiteral("http://127.0.0.1:%1/device/usercode").arg(m_server.serverPort())};
    }

    [[nodiscard]] QUrl tokenUrl() const
    {
        return {QStringLiteral("http://127.0.0.1:%1/device/token").arg(m_server.serverPort())};
    }

    [[nodiscard]] const std::vector<QByteArray> &requests() const noexcept { return m_requests; }

private:
    QTcpServer m_server;
    std::vector<QByteArray> m_requests;
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
    void deviceSessionCompletesThroughPolling();
    void refreshesAndRetainsUnrotatedRefreshToken();
    void classifiesRefreshRateLimits();
    void preparesAndParsesSubscriptionUsage();
    void rejectsInvalidSubscriptionUsage();
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
    auto &completed = requiredOptionalValue(result);
    QVERIFY(completed.has_value());
    QCOMPARE(bytes(completed.value().accessToken), QByteArrayLiteral("access"));
    QCOMPARE(bytes(completed.value().refreshToken), QByteArrayLiteral("refresh"));
    QCOMPARE(completed.value().accountId, QStringLiteral("account-1"));
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
    auto &completed = requiredOptionalValue(result);
    QVERIFY(!completed.has_value());
    QCOMPARE(completed.error().code, ztermy::ai::OpenAiSubscriptionSessionErrorCode::callbackRejected);
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
    auto &completed = requiredOptionalValue(result);
    QVERIFY(!completed.has_value());
    QCOMPARE(completed.error().code, ztermy::ai::OpenAiSubscriptionSessionErrorCode::cancelled);
    QVERIFY(!session.active());
}

void OpenAiSubscriptionAuthProtocolTests::deviceSessionCompletesThroughPolling()
{
    FakeDeviceAuthServer deviceServer;
    FakeTokenServer tokenServer;
    QNetworkAccessManager network;
    ztermy::ai::OpenAiSubscriptionAuthSession session(&network);
    std::optional<ztermy::ai::OpenAiSubscriptionDeviceCode> deviceCode;
    std::optional<ztermy::ai::OpenAiSubscriptionAuthSession::Result> result;
    ztermy::ai::OpenAiSubscriptionAuthEndpoints endpoints;
    endpoints.token = tokenServer.url();
    endpoints.deviceUserCode = deviceServer.userCodeUrl();
    endpoints.deviceToken = deviceServer.tokenUrl();
    endpoints.deviceVerification = QUrl(QStringLiteral("https://auth.example.test/codex/device"));
    endpoints.deviceRedirect = QUrl(QStringLiteral("https://auth.example.test/deviceauth/callback"));
    const auto started = session.beginDeviceLogin(
        [&deviceCode](const ztermy::ai::OpenAiSubscriptionDeviceCode &code) {
            deviceCode = code;
        },
        [&result](ztermy::ai::OpenAiSubscriptionAuthSession::Result completed) {
            result = std::move(completed);
        },
        endpoints);
    QVERIFY(started.has_value());
    QTRY_VERIFY_WITH_TIMEOUT(deviceCode.has_value(), 2000);
    QCOMPARE(requiredOptionalValue(deviceCode).verificationUrl, endpoints.deviceVerification);
    QCOMPARE(requiredOptionalValue(deviceCode).userCode, QStringLiteral("ABCD-1234"));
    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 3000);
    auto &completed = requiredOptionalValue(result);
    QVERIFY(completed.has_value());
    QCOMPARE(bytes(completed.value().accessToken), QByteArrayLiteral("access"));
    QCOMPARE(bytes(completed.value().refreshToken), QByteArrayLiteral("refresh"));
    QCOMPARE(completed.value().accountId, QStringLiteral("account-1"));
    QCOMPARE(deviceServer.requests().size(), std::size_t{2});
    QVERIFY(deviceServer.requests().front().contains(QByteArrayLiteral("\"client_id\"")));
    QVERIFY(deviceServer.requests().back().contains(QByteArrayLiteral("\"device_auth_id\":\"device-1\"")));
    QVERIFY(tokenServer.request().contains(
        QByteArrayLiteral("redirect_uri=https://auth.example.test/deviceauth/callback")));
    QVERIFY(tokenServer.request().contains(QByteArrayLiteral("code_verifier=verifier-1")));
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
    auto &completed = requiredOptionalValue(result);
    QVERIFY(completed.has_value());
    QCOMPARE(bytes(completed.value().accessToken), QByteArrayLiteral("access"));
    QCOMPARE(bytes(completed.value().refreshToken), QByteArrayLiteral("old-refresh"));
    QVERIFY(tokenServer.request().contains(QByteArrayLiteral("grant_type=refresh_token")));
    QVERIFY(tokenServer.request().contains(QByteArrayLiteral("refresh_token=old-refresh")));
    QVERIFY(!refresher.active());
}

void OpenAiSubscriptionAuthProtocolTests::classifiesRefreshRateLimits()
{
    FakeRefreshErrorServer server;
    QNetworkAccessManager network;
    ztermy::ai::OpenAiSubscriptionTokenRefresher refresher(&network);
    std::optional<ztermy::ai::OpenAiSubscriptionTokenRefresher::Result> result;
    const auto started = refresher.start(
        ztermy::security::SensitiveByteArray(QByteArrayLiteral("refresh")),
        [&result](ztermy::ai::OpenAiSubscriptionTokenRefresher::Result completed) {
            result = std::move(completed);
        },
        server.url());
    QVERIFY(started.has_value());
    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 3000);
    auto &completed = requiredOptionalValue(result);
    QVERIFY(!completed.has_value());
    QCOMPARE(completed.error().code, ztermy::ai::AiProviderErrorCode::rateLimited);
    QCOMPARE(completed.error().httpStatus, std::optional<std::uint16_t>{429});
    QCOMPARE(completed.error().retryAfterMilliseconds, std::optional<std::uint64_t>{3000});
    QVERIFY(completed.error().retryable);
    QCOMPARE(completed.error().message, std::string("Token refresh is temporarily rate limited."));
}

void OpenAiSubscriptionAuthProtocolTests::preparesAndParsesSubscriptionUsage()
{
    auto request = ztermy::ai::OpenAiSubscriptionUsage::prepareRequest(
        ztermy::security::SensitiveByteArray(QByteArrayLiteral("access-token")), QStringLiteral("account-1"),
        QStringLiteral("0.3.0"));
    QVERIFY(request.has_value());
    QCOMPARE(request->url(), QUrl(QStringLiteral("https://chatgpt.com/backend-api/codex/usage")));
    QCOMPARE(request->rawHeader("Authorization"), QByteArrayLiteral("Bearer access-token"));
    QCOMPARE(request->rawHeader("ChatGPT-Account-Id"), QByteArrayLiteral("account-1"));
    QCOMPARE(request->rawHeader("originator"), QByteArrayLiteral("ztermy"));

    const QByteArray body = QByteArrayLiteral(R"json({
        "plan_type":"prolite",
        "rate_limit":{
            "allowed":true,
            "limit_reached":false,
            "primary_window":{"used_percent":18.5,"limit_window_seconds":18000,"reset_at":2000000100},
            "secondary_window":{"used_percent":42,"limit_window_seconds":604800,"reset_at":2000000200}
        },
        "additional_rate_limits":[{
            "limit_name":"Fast model",
            "metered_feature":"codex_fast",
            "rate_limit":{"allowed":true,"limit_reached":false,
                "primary_window":{"used_percent":5,"limit_window_seconds":86400,"reset_at":2000000300},
                "secondary_window":null}
        }],
        "credits":{"has_credits":true,"unlimited":false,"balance":"12.50"}
    })json");
    auto usage = ztermy::ai::OpenAiSubscriptionUsage::parse(body);
    QVERIFY(usage.has_value());
    QCOMPARE(usage->planType, QStringLiteral("prolite"));
    QVERIFY(usage->codex.allowed);
    QVERIFY(!usage->codex.reached);
    QVERIFY(usage->codex.primary.has_value());
    QCOMPARE(requiredOptionalValue(usage->codex.primary).usedPercent, 18.5);
    QCOMPARE(requiredOptionalValue(usage->codex.secondary).durationSeconds, qint64{604800});
    QCOMPARE(usage->additional.size(), std::size_t{1});
    QCOMPARE(usage->additional.front().name, QStringLiteral("Fast model"));
    QCOMPARE(usage->creditBalance, QStringLiteral("12.50"));
}

void OpenAiSubscriptionAuthProtocolTests::rejectsInvalidSubscriptionUsage()
{
    auto missing = ztermy::ai::OpenAiSubscriptionUsage::parse(QByteArrayLiteral(R"({"plan_type":"plus"})"));
    QVERIFY(!missing.has_value());
    QCOMPARE(missing.error().code, ztermy::ai::AiProviderErrorCode::protocol);

    auto invalid = ztermy::ai::OpenAiSubscriptionUsage::parse(
        QByteArrayLiteral(R"({"plan_type":"plus","rate_limit":{"primary_window":{"used_percent":101}}})"));
    QVERIFY(!invalid.has_value());
    QCOMPARE(invalid.error().code, ztermy::ai::AiProviderErrorCode::protocol);
}

} // namespace

QTEST_GUILESS_MAIN(OpenAiSubscriptionAuthProtocolTests)

#include "openai_subscription_auth_protocol_tests.moc"
