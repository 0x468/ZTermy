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
};

class OpenAiSubscriptionAuthSession final : public QObject
{
public:
    using Result = std::expected<OpenAiSubscriptionTokenResponse, OpenAiSubscriptionSessionError>;
    using CompletionHandler = std::function<void(Result)>;

    explicit OpenAiSubscriptionAuthSession(QObject *parent = nullptr);
    explicit OpenAiSubscriptionAuthSession(QNetworkAccessManager *networkAccessManager, QObject *parent = nullptr);
    ~OpenAiSubscriptionAuthSession() override;

    OpenAiSubscriptionAuthSession(const OpenAiSubscriptionAuthSession &) = delete;
    OpenAiSubscriptionAuthSession &operator=(const OpenAiSubscriptionAuthSession &) = delete;

    [[nodiscard]] std::expected<QUrl, OpenAiSubscriptionSessionError>
    beginBrowserLogin(CompletionHandler completion, OpenAiSubscriptionAuthEndpoints endpoints = {},
                      quint16 callbackPort = 1455);
    [[nodiscard]] bool active() const noexcept;
    void cancel();

private:
    void acceptConnections();
    void consumeCallback(QTcpSocket *socket);
    void exchangeAuthorizationCode(QByteArray code);
    void finish(Result result);
    void resetTransport() noexcept;

    std::unique_ptr<QNetworkAccessManager> m_ownedNetworkAccessManager;
    QNetworkAccessManager *m_networkAccessManager = nullptr;
    QTcpServer m_callbackServer;
    QTimer m_timeout;
    QNetworkReply *m_tokenReply = nullptr;
    OpenAiSubscriptionPkce m_pkce;
    OpenAiSubscriptionAuthEndpoints m_endpoints;
    QUrl m_redirectUri;
    CompletionHandler m_completion;
};

} // namespace ztermy::ai
