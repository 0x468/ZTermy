#include "infrastructure/ai/ProviderErrorParser.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <utility>

namespace ztermy::ai
{
namespace
{

constexpr qsizetype maximumVisibleErrorBytes = 4096;

[[nodiscard]] std::string utf8(const QString &value)
{
    const auto bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] QString scalarText(const QJsonValue &value)
{
    if (value.isString())
    {
        return value.toString().trimmed();
    }
    if (value.isDouble())
    {
        return QString::number(value.toInteger());
    }
    return {};
}

template <std::size_t Size>
[[nodiscard]] QString firstText(const std::array<QJsonValue, Size> &values)
{
    for (const auto &value : values)
    {
        const QString candidate = scalarText(value);
        if (!candidate.isEmpty())
        {
            return candidate;
        }
    }
    return {};
}

[[nodiscard]] QString boundedMessage(QString message)
{
    message = message.trimmed();
    message.remove(QChar::Null);
    const auto bytes = message.toUtf8();
    if (bytes.size() <= maximumVisibleErrorBytes)
    {
        return message;
    }
    return QString::fromUtf8(bytes.first(maximumVisibleErrorBytes)).trimmed() + QStringLiteral("…");
}

[[nodiscard]] bool containsAny(const QString &value, const std::initializer_list<QStringView> needles)
{
    return std::ranges::any_of(needles, [&value](const QStringView needle) {
        return value.contains(needle, Qt::CaseInsensitive);
    });
}

[[nodiscard]] std::optional<AiProviderErrorCode> classifiedCode(const QString &providerCode,
                                                                const std::optional<std::uint16_t> httpStatus)
{
    if (containsAny(providerCode, {u"context_length", u"context_window", u"token_limit", u"payload_too_large",
                                   u"request_too_large", u"too_large"}))
    {
        return AiProviderErrorCode::contextOverflow;
    }
    if (containsAny(providerCode, {u"quota", u"billing", u"credit", u"insufficient_quota", u"payment_required"}))
    {
        return AiProviderErrorCode::quotaExceeded;
    }
    if (containsAny(providerCode, {u"auth", u"api_key", u"apikey", u"unauthorized", u"forbidden", u"permission"}))
    {
        return AiProviderErrorCode::authentication;
    }
    if (containsAny(providerCode, {u"rate", u"resource_exhausted", u"too_many_requests"}))
    {
        return AiProviderErrorCode::rateLimited;
    }
    if (containsAny(providerCode, {u"cancel"}))
    {
        return AiProviderErrorCode::cancelled;
    }
    if (containsAny(providerCode, {u"timeout", u"connection"}))
    {
        return AiProviderErrorCode::network;
    }
    if (containsAny(providerCode,
                    {u"server", u"internal", u"unavailable", u"overloaded", u"api_error", u"service_unavailable"}))
    {
        return AiProviderErrorCode::server;
    }
    if (containsAny(providerCode, {u"invalid", u"bad_request", u"malformed", u"unsupported", u"not_found"}))
    {
        return AiProviderErrorCode::invalidRequest;
    }
    if (!httpStatus.has_value())
    {
        return std::nullopt;
    }
    const auto status = *httpStatus;
    if (status == 401 || status == 403)
    {
        return AiProviderErrorCode::authentication;
    }
    if (status == 402)
    {
        return AiProviderErrorCode::quotaExceeded;
    }
    if (status == 408)
    {
        return AiProviderErrorCode::network;
    }
    if (status == 413)
    {
        return AiProviderErrorCode::contextOverflow;
    }
    if (status == 429)
    {
        return AiProviderErrorCode::rateLimited;
    }
    if (status >= 500)
    {
        return AiProviderErrorCode::server;
    }
    if (status >= 400)
    {
        return AiProviderErrorCode::invalidRequest;
    }
    return std::nullopt;
}

[[nodiscard]] bool retryable(const AiProviderErrorCode code)
{
    return code == AiProviderErrorCode::network || code == AiProviderErrorCode::rateLimited
           || code == AiProviderErrorCode::server || code == AiProviderErrorCode::contextOverflow;
}

[[nodiscard]] QString requestId(const QJsonObject &primary, const QJsonObject &root, const QJsonObject &metadata)
{
    return firstText(std::array{root.value(QStringLiteral("request_id")), root.value(QStringLiteral("requestId")),
                                primary.value(QStringLiteral("request_id")), primary.value(QStringLiteral("requestId")),
                                metadata.value(QStringLiteral("request_id")),
                                metadata.value(QStringLiteral("requestId"))});
}

} // namespace

AiProviderError parseProviderError(const QJsonObject &envelope, AiProviderError fallback)
{
    QJsonObject primary = envelope;
    if (!envelope.value(QStringLiteral("error")).isObject()
        && envelope.value(QStringLiteral("response")).toObject().value(QStringLiteral("error")).isObject())
    {
        primary = envelope.value(QStringLiteral("response")).toObject();
    }

    const QJsonValue errorValue = primary.value(QStringLiteral("error"));
    const QJsonObject error = errorValue.toObject();
    const QJsonObject metadata = error.value(QStringLiteral("metadata")).toObject();
    const QString providerCode = firstText(std::array{
        primary.value(QStringLiteral("error_type")), error.value(QStringLiteral("error_type")),
        metadata.value(QStringLiteral("error_type")), error.value(QStringLiteral("status")),
        error.value(QStringLiteral("code")), error.value(QStringLiteral("type")), primary.value(QStringLiteral("code")),
        metadata.value(QStringLiteral("provider_code")), primary.value(QStringLiteral("status"))});
    const QString message = boundedMessage(
        firstText(std::array{error.value(QStringLiteral("message")), primary.value(QStringLiteral("message")),
                             error.value(QStringLiteral("detail")), primary.value(QStringLiteral("detail")),
                             primary.value(QStringLiteral("error_description")), errorValue}));

    if (!message.isEmpty())
    {
        fallback.message = utf8(message);
    }
    if (!providerCode.isEmpty())
    {
        fallback.providerCode = utf8(providerCode);
    }
    const QString parsedRequestId = requestId(primary, envelope, metadata);
    if (!parsedRequestId.isEmpty())
    {
        fallback.requestId = utf8(parsedRequestId);
    }
    if (const auto code = classifiedCode(providerCode, fallback.httpStatus); code.has_value())
    {
        fallback.code = *code;
        fallback.retryable = retryable(*code);
    }
    return fallback;
}

AiProviderError parseProviderErrorBody(const QByteArray &body, AiProviderError fallback)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isObject())
    {
        return parseProviderError(document.object(), std::move(fallback));
    }

    QString plainText = boundedMessage(QString::fromUtf8(body));
    const bool looksLikeMarkup = plainText.startsWith(QStringLiteral("<!DOCTYPE"), Qt::CaseInsensitive)
                                 || plainText.startsWith(QStringLiteral("<html"), Qt::CaseInsensitive);
    if (!plainText.isEmpty() && !looksLikeMarkup)
    {
        fallback.message = utf8(plainText);
    }
    return fallback;
}

} // namespace ztermy::ai
