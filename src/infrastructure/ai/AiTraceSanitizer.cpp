#include "infrastructure/ai/AiTraceSanitizer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringView>

namespace ztermy::ai
{
namespace
{

[[nodiscard]] QString omittedPayload(const qsizetype encodedCharacters)
{
    return QStringLiteral("[image payload omitted from trace: %1 base64 characters]").arg(encodedCharacters);
}

[[nodiscard]] QJsonValue sanitizeValue(const QJsonValue &value, const QStringView parentKey)
{
    if (value.isString())
    {
        const QString text = value.toString();
        if (text.startsWith(QStringLiteral("data:image/"), Qt::CaseInsensitive))
        {
            const qsizetype separator = text.indexOf(QStringLiteral(";base64,"), 0, Qt::CaseInsensitive);
            if (separator >= 0)
            {
                const qsizetype payloadStart = separator + qsizetype{8};
                return text.first(payloadStart) + omittedPayload(text.size() - payloadStart);
            }
        }
        if (parentKey == QStringLiteral("images"))
        {
            return omittedPayload(text.size());
        }
        return value;
    }

    if (value.isArray())
    {
        QJsonArray sanitized;
        const QJsonArray array = value.toArray();
        for (const QJsonValue &entry : array)
        {
            sanitized.append(sanitizeValue(entry, parentKey));
        }
        return sanitized;
    }

    if (!value.isObject())
    {
        return value;
    }

    const QJsonObject object = value.toObject();
    const bool base64ImageSource = object.value(QStringLiteral("type")).toString() == QStringLiteral("base64")
                                   && object.value(QStringLiteral("media_type"))
                                          .toString()
                                          .startsWith(QStringLiteral("image/"), Qt::CaseInsensitive);
    QJsonObject sanitized;
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator)
    {
        if (base64ImageSource && iterator.key() == QStringLiteral("data") && iterator.value().isString())
        {
            sanitized.insert(iterator.key(), omittedPayload(iterator.value().toString().size()));
            continue;
        }
        sanitized.insert(iterator.key(), sanitizeValue(iterator.value(), iterator.key()));
    }
    return sanitized;
}

} // namespace

QJsonValue sanitizeAiTraceValue(const QJsonValue &value)
{
    return sanitizeValue(value, {});
}

} // namespace ztermy::ai
