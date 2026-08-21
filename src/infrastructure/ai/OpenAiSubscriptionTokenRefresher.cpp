#include "infrastructure/ai/OpenAiSubscriptionTokenRefresher.h"

#include "infrastructure/ai/ProviderErrorParser.h"

#include <QDateTime>
#include <QNetworkRequest>
#include <QPointer>

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace
{

[[nodiscard]] ztermy::security::SensitiveByteArray cloneSecret(const ztermy::security::SensitiveByteArray &secret)
{
    const std::string_view value = secret.view();
    return ztermy::security::SensitiveByteArray(QByteArray(value.data(), static_cast<qsizetype>(value.size())));
}

[[nodiscard]] std::optional<std::uint64_t> retryAfterMilliseconds(const QNetworkReply &reply)
{
    const QByteArray header = reply.rawHeader("Retry-After").trimmed();
    std::uint64_t seconds = 0;
    const auto parsed = std::from_chars(header.constData(), header.constData() + header.size(), seconds);
    if (parsed.ec == std::errc{} && parsed.ptr == header.constData() + header.size())
    {
        return seconds * 1000;
    }
    const QDateTime date = QDateTime::fromString(QString::fromLatin1(header), Qt::RFC2822Date);
    if (!date.isValid())
    {
        return std::nullopt;
    }
    const qint64 delay = date.toMSecsSinceEpoch() - QDateTime::currentMSecsSinceEpoch();
    return delay > 0 ? std::optional<std::uint64_t>{static_cast<std::uint64_t>(delay)}
                     : std::optional<std::uint64_t>{1};
}

[[nodiscard]] ztermy::ai::AiProviderError refreshError(QNetworkReply &reply, const int status)
{
    using ztermy::ai::AiProviderError;
    using ztermy::ai::AiProviderErrorCode;
    AiProviderErrorCode code = AiProviderErrorCode::network;
    bool retryable = reply.error() != QNetworkReply::SslHandshakeFailedError;
    if (status == 400 || status == 401 || status == 403)
    {
        code = AiProviderErrorCode::authentication;
        retryable = false;
    }
    else if (status == 402)
    {
        code = AiProviderErrorCode::quotaExceeded;
        retryable = false;
    }
    else if (status == 429)
    {
        code = AiProviderErrorCode::rateLimited;
        retryable = true;
    }
    else if (status >= 500)
    {
        code = AiProviderErrorCode::server;
        retryable = true;
    }
    else if (status >= 400)
    {
        code = AiProviderErrorCode::invalidRequest;
        retryable = false;
    }
    constexpr qsizetype maximumErrorBytes = qsizetype{64} * 1024;
    QByteArray body = reply.read(maximumErrorBytes + 1);
    if (body.size() > maximumErrorBytes)
    {
        body.truncate(maximumErrorBytes);
    }
    AiProviderError fallback{
        .code = code,
        .message = code == AiProviderErrorCode::authentication ? "ChatGPT sign-in expired. Sign in again."
                                                               : "Could not refresh the ChatGPT sign-in.",
        .retryAfterMilliseconds = retryAfterMilliseconds(reply),
        .retryable = retryable,
        .httpStatus = status > 0 ? std::optional<std::uint16_t>{static_cast<std::uint16_t>(status)} : std::nullopt,
    };
    return ztermy::ai::parseProviderErrorBody(body, std::move(fallback));
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
    request.setRawHeader("User-Agent", "ztermy/0.3.0");
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
