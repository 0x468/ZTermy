#pragma once

#include "core/security/SensitiveByteArray.h"
#include "domain/ai/AiProviderTypes.h"
#include "infrastructure/ai/OpenAiSubscriptionAuthProtocol.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QUrl>

#include <expected>
#include <functional>
#include <memory>

namespace ztermy::ai
{

class OpenAiSubscriptionTokenRefresher final : public QObject
{
public:
    using Result = std::expected<OpenAiSubscriptionTokenResponse, AiProviderError>;
    using CompletionHandler = std::function<void(Result)>;

    explicit OpenAiSubscriptionTokenRefresher(QObject *parent = nullptr);
    explicit OpenAiSubscriptionTokenRefresher(QNetworkAccessManager *networkAccessManager, QObject *parent = nullptr);
    ~OpenAiSubscriptionTokenRefresher() override;

    OpenAiSubscriptionTokenRefresher(const OpenAiSubscriptionTokenRefresher &) = delete;
    OpenAiSubscriptionTokenRefresher &operator=(const OpenAiSubscriptionTokenRefresher &) = delete;

    [[nodiscard]] std::expected<void, AiProviderError>
    start(security::SensitiveByteArray refreshToken, CompletionHandler completion,
          const QUrl &tokenEndpoint = OpenAiSubscriptionAuthProtocol::tokenUrl());
    [[nodiscard]] bool active() const noexcept;
    void cancel();

private:
    void finish(Result result);

    std::unique_ptr<QNetworkAccessManager> m_ownedNetworkAccessManager;
    QNetworkAccessManager *m_networkAccessManager = nullptr;
    QNetworkReply *m_reply = nullptr;
    security::SensitiveByteArray m_previousRefreshToken;
    CompletionHandler m_completion;
};

} // namespace ztermy::ai
