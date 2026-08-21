#pragma once

#include "infrastructure/ai/OpenAiSubscriptionAuthProtocol.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QTcpServer>
#include <QTimer>

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>

namespace ztermy::ai
{

enum class OpenAiSubscriptionSessionErrorCode : std::uint8_t
{
    alreadyActive,
    callbackUnavailable,
    callbackRejected,
    authorizationDenied,
    network,
    invalidResponse,
    cancelled,
    timedOut,
};

struct OpenAiSubscriptionSessionError final
{
    OpenAiSubscriptionSessionErrorCode code = OpenAiSubscriptionSessionErrorCode::network;
    QString message;
};

struct OpenAiSubscriptionAuthEndpoints final
{
    QUrl authorization = OpenAiSubscriptionAuthProtocol::issuerUrl().resolved(QUrl(QStringLiteral("/oauth/authorize")));
    QUrl token = OpenAiSubscriptionAuthProtocol::tokenUrl();
    QUrl deviceUserCode =
        OpenAiSubscriptionAuthProtocol::issuerUrl().resolved(QUrl(QStringLiteral("/api/accounts/deviceauth/usercode")));
    QUrl deviceToken =
        OpenAiSubscriptionAuthProtocol::issuerUrl().resolved(QUrl(QStringLiteral("/api/accounts/deviceauth/token")));
    QUrl deviceVerification =
        OpenAiSubscriptionAuthProtocol::issuerUrl().resolved(QUrl(QStringLiteral("/codex/device")));
    QUrl deviceRedirect =
        OpenAiSubscriptionAuthProtocol::issuerUrl().resolved(QUrl(QStringLiteral("/deviceauth/callback")));
};

struct OpenAiSubscriptionDeviceCode final
{
    QUrl verificationUrl;
    QString userCode;
};

class OpenAiSubscriptionAuthSession final : public QObject
{
public:
    using Result = std::expected<OpenAiSubscriptionTokenResponse, OpenAiSubscriptionSessionError>;
    using CompletionHandler = std::function<void(Result)>;
    using DeviceCodeHandler = std::function<void(const OpenAiSubscriptionDeviceCode &)>;

    explicit OpenAiSubscriptionAuthSession(QObject *parent = nullptr);
    explicit OpenAiSubscriptionAuthSession(QNetworkAccessManager *networkAccessManager, QObject *parent = nullptr);
    ~OpenAiSubscriptionAuthSession() override;

    OpenAiSubscriptionAuthSession(const OpenAiSubscriptionAuthSession &) = delete;
    OpenAiSubscriptionAuthSession &operator=(const OpenAiSubscriptionAuthSession &) = delete;

    [[nodiscard]] std::expected<QUrl, OpenAiSubscriptionSessionError>
    beginBrowserLogin(CompletionHandler completion, OpenAiSubscriptionAuthEndpoints endpoints = {},
                      quint16 callbackPort = 1455);
    [[nodiscard]] std::expected<void, OpenAiSubscriptionSessionError>
    beginDeviceLogin(DeviceCodeHandler deviceCodeHandler, CompletionHandler completion,
                     OpenAiSubscriptionAuthEndpoints endpoints = {});
    [[nodiscard]] bool active() const noexcept;
    void cancel();

private:
    void acceptConnections();
    void consumeCallback(QTcpSocket *socket);
    void requestDeviceCode();
    void pollDeviceAuthorization();
    void exchangeAuthorizationCode(const QByteArray &code);
    void finish(Result result);
    void resetTransport() noexcept;

    std::unique_ptr<QNetworkAccessManager> m_ownedNetworkAccessManager;
    QNetworkAccessManager *m_networkAccessManager = nullptr;
    QTcpServer m_callbackServer;
    QTimer m_timeout;
    QTimer m_devicePollTimer;
    QNetworkReply *m_tokenReply = nullptr;
    OpenAiSubscriptionPkce m_pkce;
    OpenAiSubscriptionAuthEndpoints m_endpoints;
    QUrl m_redirectUri;
    QByteArray m_deviceAuthId;
    QByteArray m_deviceUserCode;
    int m_devicePollIntervalMilliseconds = 5'000;
    DeviceCodeHandler m_deviceCodeHandler;
    CompletionHandler m_completion;
};

} // namespace ztermy::ai
