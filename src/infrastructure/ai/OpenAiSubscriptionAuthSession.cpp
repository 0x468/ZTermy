#include "infrastructure/ai/OpenAiSubscriptionAuthSession.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QPointer>
#include <QTcpSocket>
#include <QUrlQuery>

#include <algorithm>

namespace
{

constexpr qsizetype MaximumCallbackBytes = qsizetype{16} * 1024;
constexpr qsizetype MaximumDeviceResponseBytes = qsizetype{256} * 1024;
constexpr int AuthorizationTimeoutMilliseconds = 5 * 60 * 1000;
constexpr int DeviceAuthorizationTimeoutMilliseconds = 15 * 60 * 1000;
constexpr int MinimumDevicePollIntervalMilliseconds = 1'000;
constexpr int MaximumDevicePollIntervalMilliseconds = 30'000;

void replyToBrowser(QTcpSocket *socket, const int status, const QByteArrayView title, const QByteArrayView message)
{
    if (socket == nullptr)
    {
        return;
    }
    const QByteArray titleBytes(title.data(), title.size());
    const QByteArray messageBytes(message.data(), message.size());
    const QByteArray body = QByteArrayLiteral("<!doctype html><meta charset=utf-8><title>") + titleBytes
                            + QByteArrayLiteral("</title><style>body{font:16px system-ui;margin:4rem;max-width:42rem}"
                                                "h1{font-size:1.5rem}</style><h1>")
                            + titleBytes + QByteArrayLiteral("</h1><p>") + messageBytes + QByteArrayLiteral("</p>");
    QByteArray response =
        status == 200 ? QByteArrayLiteral("HTTP/1.1 200 OK\r\n") : QByteArrayLiteral("HTTP/1.1 400 Bad Request\r\n");
    response += QByteArrayLiteral("Content-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: ")
                + QByteArray::number(body.size()) + QByteArrayLiteral("\r\n\r\n") + body;
    socket->write(response);
    socket->disconnectFromHost();
}

} // namespace

namespace ztermy::ai
{

OpenAiSubscriptionAuthSession::OpenAiSubscriptionAuthSession(QObject *parent)
    : QObject(parent),
      m_ownedNetworkAccessManager(std::make_unique<QNetworkAccessManager>()),
      m_networkAccessManager(m_ownedNetworkAccessManager.get())
{
    QObject::connect(&m_callbackServer, &QTcpServer::newConnection, this,
                     &OpenAiSubscriptionAuthSession::acceptConnections);
    m_timeout.setSingleShot(true);
    m_devicePollTimer.setSingleShot(true);
    QObject::connect(&m_devicePollTimer, &QTimer::timeout, this,
                     &OpenAiSubscriptionAuthSession::pollDeviceAuthorization);
    QObject::connect(&m_timeout, &QTimer::timeout, this, [this] {
        finish(std::unexpected(OpenAiSubscriptionSessionError{
            .code = OpenAiSubscriptionSessionErrorCode::timedOut,
            .message = tr("ChatGPT sign-in timed out. Try again."),
        }));
    });
}

OpenAiSubscriptionAuthSession::OpenAiSubscriptionAuthSession(QNetworkAccessManager *networkAccessManager,
                                                             QObject *parent)
    : QObject(parent), m_networkAccessManager(networkAccessManager)
{
    QObject::connect(&m_callbackServer, &QTcpServer::newConnection, this,
                     &OpenAiSubscriptionAuthSession::acceptConnections);
    m_timeout.setSingleShot(true);
    m_devicePollTimer.setSingleShot(true);
    QObject::connect(&m_devicePollTimer, &QTimer::timeout, this,
                     &OpenAiSubscriptionAuthSession::pollDeviceAuthorization);
    QObject::connect(&m_timeout, &QTimer::timeout, this, [this] {
        finish(std::unexpected(OpenAiSubscriptionSessionError{
            .code = OpenAiSubscriptionSessionErrorCode::timedOut,
            .message = tr("ChatGPT sign-in timed out. Try again."),
        }));
    });
}

OpenAiSubscriptionAuthSession::~OpenAiSubscriptionAuthSession()
{
    m_completion = {};
    resetTransport();
}

std::expected<QUrl, OpenAiSubscriptionSessionError>
OpenAiSubscriptionAuthSession::beginBrowserLogin(CompletionHandler completion,
                                                 OpenAiSubscriptionAuthEndpoints endpoints, const quint16 callbackPort)
{
    if (active())
    {
        return std::unexpected(OpenAiSubscriptionSessionError{
            .code = OpenAiSubscriptionSessionErrorCode::alreadyActive,
            .message = tr("ChatGPT sign-in is already in progress."),
        });
    }
    if (m_networkAccessManager == nullptr || !completion || !endpoints.authorization.isValid()
        || !endpoints.token.isValid() || !m_callbackServer.listen(QHostAddress::LocalHost, callbackPort))
    {
        return std::unexpected(OpenAiSubscriptionSessionError{
            .code = OpenAiSubscriptionSessionErrorCode::callbackUnavailable,
            .message = tr("Could not start the local ChatGPT sign-in callback."),
        });
    }

    m_pkce = OpenAiSubscriptionAuthProtocol::createPkce();
    m_endpoints = std::move(endpoints);
    // Keep the registered ecosystem callback spelling. The listener remains
    // loopback-only; "localhost" is part of the OAuth redirect identity.
    m_redirectUri = QUrl(QStringLiteral("http://localhost:%1/auth/callback").arg(m_callbackServer.serverPort()));
    m_completion = std::move(completion);
    m_timeout.start(AuthorizationTimeoutMilliseconds);
    return OpenAiSubscriptionAuthProtocol::authorizationUrl(m_pkce, m_redirectUri, m_endpoints.authorization);
}

std::expected<void, OpenAiSubscriptionSessionError>
OpenAiSubscriptionAuthSession::beginDeviceLogin(DeviceCodeHandler deviceCodeHandler, CompletionHandler completion,
                                                OpenAiSubscriptionAuthEndpoints endpoints)
{
    if (active())
    {
        return std::unexpected(OpenAiSubscriptionSessionError{
            .code = OpenAiSubscriptionSessionErrorCode::alreadyActive,
            .message = tr("ChatGPT sign-in is already in progress."),
        });
    }
    if (m_networkAccessManager == nullptr || !deviceCodeHandler || !completion || !endpoints.deviceUserCode.isValid()
        || !endpoints.deviceToken.isValid() || !endpoints.deviceVerification.isValid()
        || !endpoints.deviceRedirect.isValid() || !endpoints.token.isValid())
    {
        return std::unexpected(OpenAiSubscriptionSessionError{
            .code = OpenAiSubscriptionSessionErrorCode::network,
            .message = tr("Could not start ChatGPT device-code sign-in."),
        });
    }

    m_endpoints = std::move(endpoints);
    m_redirectUri = m_endpoints.deviceRedirect;
    m_deviceCodeHandler = std::move(deviceCodeHandler);
    m_completion = std::move(completion);
    m_timeout.start(DeviceAuthorizationTimeoutMilliseconds);
    requestDeviceCode();
    return {};
}

bool OpenAiSubscriptionAuthSession::active() const noexcept
{
    return static_cast<bool>(m_completion);
}

void OpenAiSubscriptionAuthSession::cancel()
{
    if (!active())
    {
        return;
    }
    finish(std::unexpected(OpenAiSubscriptionSessionError{
        .code = OpenAiSubscriptionSessionErrorCode::cancelled,
        .message = tr("ChatGPT sign-in was cancelled."),
    }));
}

void OpenAiSubscriptionAuthSession::acceptConnections()
{
    while (m_callbackServer.hasPendingConnections())
    {
        QTcpSocket *socket = m_callbackServer.nextPendingConnection();
        if (socket == nullptr)
        {
            continue;
        }
        socket->setParent(this);
        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, guarded = QPointer<QTcpSocket>(socket)] {
            if (guarded)
            {
                consumeCallback(guarded);
            }
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void OpenAiSubscriptionAuthSession::consumeCallback(QTcpSocket *socket)
{
    if (!active() || socket->bytesAvailable() > MaximumCallbackBytes)
    {
        replyToBrowser(socket, 400, "ztermy", "The sign-in callback was rejected.");
        return;
    }
    const QByteArray request = socket->peek(MaximumCallbackBytes);
    const qsizetype headerEnd = request.indexOf(QByteArrayLiteral("\r\n\r\n"));
    if (headerEnd < 0)
    {
        return;
    }
    socket->read(headerEnd + 4);
    const qsizetype lineEnd = request.indexOf(QByteArrayLiteral("\r\n"));
    const QList<QByteArray> requestParts = request.first(lineEnd).split(' ');
    if (lineEnd <= 0 || requestParts.size() != 3 || requestParts.at(0) != QByteArrayLiteral("GET"))
    {
        replyToBrowser(socket, 400, "ztermy", "The sign-in callback was invalid.");
        return;
    }

    const QUrl callback = QUrl::fromEncoded(requestParts.at(1));
    const QUrlQuery query(callback);
    if (callback.path() != QStringLiteral("/auth/callback")
        || query.queryItemValue(QStringLiteral("state")).toLatin1() != m_pkce.state)
    {
        replyToBrowser(socket, 400, "ztermy", "The sign-in callback did not match this request.");
        finish(std::unexpected(OpenAiSubscriptionSessionError{
            .code = OpenAiSubscriptionSessionErrorCode::callbackRejected,
            .message = tr("ChatGPT returned an invalid sign-in callback."),
        }));
        return;
    }
    const QString authorizationError = query.queryItemValue(QStringLiteral("error_description"));
    const QString error = query.queryItemValue(QStringLiteral("error"));
    if (!error.isEmpty())
    {
        replyToBrowser(socket, 400, "ChatGPT sign-in cancelled", "You can return to ztermy.");
        finish(std::unexpected(OpenAiSubscriptionSessionError{
            .code = OpenAiSubscriptionSessionErrorCode::authorizationDenied,
            .message = authorizationError.isEmpty() ? tr("ChatGPT sign-in was not authorized.") : authorizationError,
        }));
        return;
    }
    const QByteArray code = query.queryItemValue(QStringLiteral("code")).toLatin1();
    if (code.isEmpty())
    {
        replyToBrowser(socket, 400, "ztermy", "The authorization code was missing.");
        finish(std::unexpected(OpenAiSubscriptionSessionError{
            .code = OpenAiSubscriptionSessionErrorCode::callbackRejected,
            .message = tr("ChatGPT did not return an authorization code."),
        }));
        return;
    }

    replyToBrowser(socket, 200, "ChatGPT sign-in complete", "You can close this page and return to ztermy.");
    m_callbackServer.close();
    m_timeout.stop();
    exchangeAuthorizationCode(code);
}

void OpenAiSubscriptionAuthSession::requestDeviceCode()
{
    if (!active())
    {
        return;
    }
    const QJsonObject payload{
        {QStringLiteral("client_id"), QString::fromLatin1(OpenAiSubscriptionAuthProtocol::clientId())}};
    QNetworkRequest request(m_endpoints.deviceUserCode);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(30'000);
    m_tokenReply = m_networkAccessManager->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QObject::connect(m_tokenReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_tokenReply;
        m_tokenReply = nullptr;
        if (reply == nullptr || !active())
        {
            if (reply != nullptr)
            {
                reply->deleteLater();
            }
            return;
        }
        QByteArray body = reply->read(MaximumDeviceResponseBytes + 1);
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool successful = reply->error() == QNetworkReply::NoError && status / 100 == 2;
        reply->deleteLater();
        if (!successful)
        {
            finish(std::unexpected(OpenAiSubscriptionSessionError{
                .code = OpenAiSubscriptionSessionErrorCode::network,
                .message = status == 404 ? tr("ChatGPT device-code sign-in is not enabled for this account.")
                                         : tr("Could not request a ChatGPT device code."),
            }));
            return;
        }
        if (body.size() > MaximumDeviceResponseBytes)
        {
            finish(std::unexpected(OpenAiSubscriptionSessionError{
                .code = OpenAiSubscriptionSessionErrorCode::invalidResponse,
                .message = tr("ChatGPT returned an invalid device-code response."),
            }));
            return;
        }
        const QJsonObject object = QJsonDocument::fromJson(body).object();
        m_deviceAuthId = object.value(QStringLiteral("device_auth_id")).toString().toUtf8();
        QString userCode = object.value(QStringLiteral("user_code")).toString();
        if (userCode.isEmpty())
        {
            userCode = object.value(QStringLiteral("usercode")).toString();
        }
        m_deviceUserCode = userCode.toUtf8();
        const QJsonValue intervalValue = object.value(QStringLiteral("interval"));
        bool intervalOk = intervalValue.isDouble();
        const int intervalSeconds =
            intervalValue.isString() ? intervalValue.toString().toInt(&intervalOk) : intervalValue.toInt();
        m_devicePollIntervalMilliseconds =
            std::clamp((intervalOk ? intervalSeconds : 5) * 1'000, MinimumDevicePollIntervalMilliseconds,
                       MaximumDevicePollIntervalMilliseconds);
        if (m_deviceAuthId.isEmpty() || m_deviceUserCode.isEmpty())
        {
            finish(std::unexpected(OpenAiSubscriptionSessionError{
                .code = OpenAiSubscriptionSessionErrorCode::invalidResponse,
                .message = tr("ChatGPT returned an invalid device-code response."),
            }));
            return;
        }
        if (m_deviceCodeHandler)
        {
            m_deviceCodeHandler(OpenAiSubscriptionDeviceCode{
                .verificationUrl = m_endpoints.deviceVerification,
                .userCode = QString::fromUtf8(m_deviceUserCode),
            });
        }
        pollDeviceAuthorization();
    });
}

void OpenAiSubscriptionAuthSession::pollDeviceAuthorization()
{
    if (!active() || m_tokenReply != nullptr || m_deviceAuthId.isEmpty() || m_deviceUserCode.isEmpty())
    {
        return;
    }
    const QJsonObject payload{{QStringLiteral("device_auth_id"), QString::fromUtf8(m_deviceAuthId)},
                              {QStringLiteral("user_code"), QString::fromUtf8(m_deviceUserCode)}};
    QNetworkRequest request(m_endpoints.deviceToken);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(30'000);
    m_tokenReply = m_networkAccessManager->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QObject::connect(m_tokenReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_tokenReply;
        m_tokenReply = nullptr;
        if (reply == nullptr || !active())
        {
            if (reply != nullptr)
            {
                reply->deleteLater();
            }
            return;
        }
        QByteArray body = reply->read(MaximumDeviceResponseBytes + 1);
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool successful = reply->error() == QNetworkReply::NoError && status / 100 == 2;
        reply->deleteLater();
        if (!successful && (status == 403 || status == 404))
        {
            m_devicePollTimer.start(m_devicePollIntervalMilliseconds);
            return;
        }
        if (!successful)
        {
            finish(std::unexpected(OpenAiSubscriptionSessionError{
                .code = OpenAiSubscriptionSessionErrorCode::network,
                .message = tr("Could not complete ChatGPT device-code sign-in."),
            }));
            return;
        }
        if (body.size() > MaximumDeviceResponseBytes)
        {
            finish(std::unexpected(OpenAiSubscriptionSessionError{
                .code = OpenAiSubscriptionSessionErrorCode::invalidResponse,
                .message = tr("ChatGPT returned an invalid device-code response."),
            }));
            return;
        }
        const QJsonObject object = QJsonDocument::fromJson(body).object();
        const QByteArray code = object.value(QStringLiteral("authorization_code")).toString().toUtf8();
        m_pkce.verifier = object.value(QStringLiteral("code_verifier")).toString().toUtf8();
        m_pkce.challenge = object.value(QStringLiteral("code_challenge")).toString().toUtf8();
        if (code.isEmpty() || m_pkce.verifier.isEmpty())
        {
            finish(std::unexpected(OpenAiSubscriptionSessionError{
                .code = OpenAiSubscriptionSessionErrorCode::invalidResponse,
                .message = tr("ChatGPT returned an invalid device-code response."),
            }));
            return;
        }
        exchangeAuthorizationCode(code);
    });
}

void OpenAiSubscriptionAuthSession::exchangeAuthorizationCode(const QByteArray &code)
{
    QNetworkRequest request(m_endpoints.token);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    m_tokenReply = m_networkAccessManager->post(
        request, OpenAiSubscriptionAuthProtocol::authorizationCodeForm(code, m_pkce.verifier, m_redirectUri));
    QObject::connect(m_tokenReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_tokenReply;
        m_tokenReply = nullptr;
        if (reply == nullptr)
        {
            return;
        }
        const QByteArray body = reply->readAll();
        const bool successful = reply->error() == QNetworkReply::NoError
                                && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() / 100 == 2;
        reply->deleteLater();
        if (!successful)
        {
            finish(std::unexpected(OpenAiSubscriptionSessionError{
                .code = OpenAiSubscriptionSessionErrorCode::network,
                .message = tr("Could not complete ChatGPT sign-in."),
            }));
            return;
        }
        auto tokens = OpenAiSubscriptionAuthProtocol::parseTokenResponse(body, true);
        if (!tokens)
        {
            finish(std::unexpected(OpenAiSubscriptionSessionError{
                .code = OpenAiSubscriptionSessionErrorCode::invalidResponse,
                .message = tr("ChatGPT returned an incomplete sign-in response."),
            }));
            return;
        }
        finish(std::move(*tokens));
    });
}

void OpenAiSubscriptionAuthSession::finish(Result result)
{
    CompletionHandler completion = std::move(m_completion);
    resetTransport();
    if (completion)
    {
        completion(std::move(result));
    }
}

void OpenAiSubscriptionAuthSession::resetTransport() noexcept
{
    m_timeout.stop();
    m_devicePollTimer.stop();
    m_callbackServer.close();
    if (m_tokenReply != nullptr)
    {
        m_tokenReply->abort();
        m_tokenReply->deleteLater();
        m_tokenReply = nullptr;
    }
    m_pkce = {};
    m_redirectUri = QUrl{};
    m_deviceAuthId.clear();
    m_deviceUserCode.clear();
    m_deviceCodeHandler = {};
}

} // namespace ztermy::ai
