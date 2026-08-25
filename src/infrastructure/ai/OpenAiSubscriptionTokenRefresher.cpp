#include "infrastructure/ai/OpenAiSubscriptionTokenRefresher.h"

#include "infrastructure/ai/ProviderErrorParser.h"

#include <QNetworkRequest>
#include <QPointer>

#include <cstdint>
#include <string_view>
#include <utility>

namespace
{

[[nodiscard]] ztermy::security::SensitiveByteArray cloneSecret(const ztermy::security::SensitiveByteArray &secret)
{
    const std::string_view value = secret.view();
    return ztermy::security::SensitiveByteArray(QByteArray(value.data(), static_cast<qsizetype>(value.size())));
}

[[nodiscard]] ztermy::ai::AiProviderError refreshError(QNetworkReply &reply, const int status)
{
    constexpr qsizetype maximumErrorBytes = qsizetype{64} * 1024;
    QByteArray body = reply.read(maximumErrorBytes + 1);
    if (body.size() > maximumErrorBytes)
    {
        body.truncate(maximumErrorBytes);
    }
    return ztermy::ai::parseProviderReplyError(reply, body,
                                               status == 400 || status == 401 || status == 403
                                                   ? "ChatGPT sign-in expired. Sign in again."
                                                   : "Could not refresh the ChatGPT sign-in.");
}

} // namespace

namespace ztermy::ai
{

OpenAiSubscriptionTokenRefresher::OpenAiSubscriptionTokenRefresher(QObject *parent)
    : QObject(parent),
      m_ownedNetworkAccessManager(std::make_unique<QNetworkAccessManager>()),
      m_networkAccessManager(m_ownedNetworkAccessManager.get())
{
}

OpenAiSubscriptionTokenRefresher::OpenAiSubscriptionTokenRefresher(QNetworkAccessManager *networkAccessManager,
                                                                   QObject *parent)
    : QObject(parent), m_networkAccessManager(networkAccessManager)
{
}

OpenAiSubscriptionTokenRefresher::~OpenAiSubscriptionTokenRefresher()
{
    m_completion = {};
    if (m_reply != nullptr)
    {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_previousRefreshToken.clear();
}

std::expected<void, AiProviderError> OpenAiSubscriptionTokenRefresher::start(security::SensitiveByteArray refreshToken,
                                                                             CompletionHandler completion,
                                                                             const QUrl &tokenEndpoint)
{
    if (active())
    {
        refreshToken.clear();
        return std::unexpected(AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                                               .message = "A ChatGPT token refresh is already active.",
                                               .retryable = false});
    }
    if (m_networkAccessManager == nullptr || refreshToken.empty() || !completion || !tokenEndpoint.isValid()
        || tokenEndpoint.scheme() != QStringLiteral("https") && tokenEndpoint.scheme() != QStringLiteral("http"))
    {
        refreshToken.clear();
        return std::unexpected(AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                                               .message = "The ChatGPT refresh request is incomplete.",
                                               .retryable = false});
    }

    m_previousRefreshToken = cloneSecret(refreshToken);
    const QByteArray form = OpenAiSubscriptionAuthProtocol::refreshTokenForm(refreshToken.view());
    refreshToken.clear();
    QNetworkRequest request(tokenEndpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "ztermy/0.4.2");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30'000);
    m_completion = std::move(completion);
    m_reply = m_networkAccessManager->post(request, form);
    const QPointer<OpenAiSubscriptionTokenRefresher> guardedThis(this);
    QObject::connect(m_reply, &QNetworkReply::finished, this, [guardedThis] {
        if (!guardedThis || guardedThis->m_reply == nullptr)
        {
            return;
        }
        QNetworkReply *reply = guardedThis->m_reply;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status >= 400)
        {
            guardedThis->finish(std::unexpected(refreshError(*reply, status)));
            return;
        }
        constexpr qsizetype maximumTokenResponseBytes = qsizetype{256} * 1024;
        const QByteArray body = reply->read(maximumTokenResponseBytes + 1);
        if (body.size() > maximumTokenResponseBytes)
        {
            guardedThis->finish(
                std::unexpected(AiProviderError{.code = AiProviderErrorCode::protocol,
                                                .message = "ChatGPT returned an oversized token response.",
                                                .retryable = false}));
            return;
        }
        auto parsed = OpenAiSubscriptionAuthProtocol::parseTokenResponse(body, false);
        if (!parsed.has_value())
        {
            guardedThis->finish(
                std::unexpected(AiProviderError{.code = AiProviderErrorCode::protocol,
                                                .message = "ChatGPT returned an invalid token response.",
                                                .retryable = false}));
            return;
        }
        if (parsed->refreshToken.empty())
        {
            parsed->refreshToken = cloneSecret(guardedThis->m_previousRefreshToken);
        }
        if (parsed->accountId.isEmpty())
        {
            parsed->accountId = OpenAiSubscriptionAuthProtocol::accountIdFromAccessToken(parsed->accessToken.view());
        }
        guardedThis->finish(std::move(*parsed));
    });
    return {};
}

bool OpenAiSubscriptionTokenRefresher::active() const noexcept
{
    return m_reply != nullptr;
}

void OpenAiSubscriptionTokenRefresher::cancel()
{
    if (!active())
    {
        return;
    }
    m_reply->disconnect(this);
    m_reply->abort();
    finish(std::unexpected(AiProviderError{.code = AiProviderErrorCode::cancelled,
                                           .message = "ChatGPT token refresh was cancelled.",
                                           .retryable = false}));
}

void OpenAiSubscriptionTokenRefresher::finish(Result result)
{
    if (!m_completion)
    {
        return;
    }
    CompletionHandler completion = std::move(m_completion);
    if (m_reply != nullptr)
    {
        m_reply->disconnect(this);
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_previousRefreshToken.clear();
    completion(std::move(result));
}

} // namespace ztermy::ai
