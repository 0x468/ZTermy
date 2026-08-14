#include "infrastructure/ai/AiQuickMessageStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace
{

constexpr qint64 maximumFileBytes = qint64{3} * 1024 * 1024;
constexpr qint64 schemaVersion = 1;

[[nodiscard]] std::optional<std::int64_t> parseTimestamp(const QJsonValue &value)
{
    if (!value.isDouble())
    {
        return std::nullopt;
    }
    const double number = value.toDouble(-1.0);
    if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number
        || number > static_cast<double>(std::numeric_limits<std::int64_t>::max()))
    {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(number);
}

[[nodiscard]] std::optional<ztermy::ai::AiQuickMessage> parseMessage(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const auto createdUtcMs = parseTimestamp(object.value(QStringLiteral("createdUtcMs")));
    const auto modifiedUtcMs = parseTimestamp(object.value(QStringLiteral("modifiedUtcMs")));
    if (!object.value(QStringLiteral("id")).isString() || !object.value(QStringLiteral("name")).isString()
        || !object.value(QStringLiteral("slug")).isString() || !object.value(QStringLiteral("content")).isString()
        || !object.value(QStringLiteral("description")).isString() || !createdUtcMs || !modifiedUtcMs)
    {
        return std::nullopt;
    }
    const auto utf8 = [](const QString &text) {
        const QByteArray bytes = text.toUtf8();
        return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    };
    ztermy::ai::AiQuickMessage message{
        .id = utf8(object.value(QStringLiteral("id")).toString()),
        .name = utf8(object.value(QStringLiteral("name")).toString()),
        .slug = utf8(object.value(QStringLiteral("slug")).toString()),
        .content = utf8(object.value(QStringLiteral("content")).toString()),
        .description = utf8(object.value(QStringLiteral("description")).toString()),
        .createdUtcMs = *createdUtcMs,
        .modifiedUtcMs = *modifiedUtcMs,
    };
    return ztermy::ai::validAiQuickMessage(message) ? std::optional{std::move(message)} : std::nullopt;
}

[[nodiscard]] QJsonObject serializeMessage(const ztermy::ai::AiQuickMessage &message)
{
    const auto text = [](const std::string &value) {
        return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    };
    return {
        {QStringLiteral("id"), text(message.id)},
        {QStringLiteral("name"), text(message.name)},
        {QStringLiteral("slug"), text(message.slug)},
        {QStringLiteral("content"), text(message.content)},
        {QStringLiteral("description"), text(message.description)},
        {QStringLiteral("createdUtcMs"), message.createdUtcMs},
        {QStringLiteral("modifiedUtcMs"), message.modifiedUtcMs},
    };
}

[[nodiscard]] bool hasDuplicates(const std::span<const ztermy::ai::AiQuickMessage> messages)
{
    for (std::size_t index = 0; index < messages.size(); ++index)
    {
        const auto duplicate =
            std::find_if(messages.begin() + static_cast<std::ptrdiff_t>(index + 1), messages.end(),
                         [&messages, index](const ztermy::ai::AiQuickMessage &candidate) {
                             return candidate.id == messages[index].id || candidate.slug == messages[index].slug;
                         });
        if (duplicate != messages.end())
        {
            return true;
        }
    }
    return false;
}

} // namespace

namespace ztermy::ai
{

AiQuickMessageStore::AiQuickMessageStore(QString filePath) : m_filePath(std::move(filePath)) {}

const QString &AiQuickMessageStore::filePath() const noexcept
{
    return m_filePath;
}

std::expected<std::vector<AiQuickMessage>, AiQuickMessageStoreError> AiQuickMessageStore::load() const
{
    if (m_filePath.isEmpty())
    {
        return std::unexpected(AiQuickMessageStoreError::invalidPath);
    }
    QFile file(m_filePath);
    if (!file.exists())
    {
        return std::vector<AiQuickMessage>{};
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() < 0 || file.size() > maximumFileBytes)
    {
        return std::unexpected(file.size() > maximumFileBytes ? AiQuickMessageStoreError::invalidFormat
                                                              : AiQuickMessageStoreError::ioError);
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(AiQuickMessageStoreError::invalidFormat);
    }
    const QJsonObject root = document.object();
    if (!root.value(QStringLiteral("version")).isDouble())
    {
        return std::unexpected(AiQuickMessageStoreError::invalidFormat);
    }
    if (root.value(QStringLiteral("version")).toInteger(-1) != schemaVersion)
    {
        return std::unexpected(AiQuickMessageStoreError::unsupportedVersion);
    }
    const QJsonValue values = root.value(QStringLiteral("messages"));
    if (!values.isArray() || values.toArray().size() > maximumAiQuickMessageCount)
    {
        return std::unexpected(AiQuickMessageStoreError::invalidFormat);
    }
    std::vector<AiQuickMessage> messages;
    messages.reserve(static_cast<std::size_t>(values.toArray().size()));
    for (const QJsonValue value : values.toArray())
    {
        auto message = parseMessage(value);
        if (!message)
        {
            return std::unexpected(AiQuickMessageStoreError::invalidFormat);
        }
        messages.push_back(std::move(*message));
    }
    if (hasDuplicates(messages))
    {
        return std::unexpected(AiQuickMessageStoreError::invalidFormat);
    }
    return messages;
}

std::expected<void, AiQuickMessageStoreError>
AiQuickMessageStore::save(const std::span<const AiQuickMessage> messages) const
{
    if (m_filePath.isEmpty())
    {
        return std::unexpected(AiQuickMessageStoreError::invalidPath);
    }
    if (messages.size() > static_cast<std::size_t>(maximumAiQuickMessageCount) || hasDuplicates(messages)
        || std::ranges::any_of(messages, [](const AiQuickMessage &message) {
               return !validAiQuickMessage(message);
           }))
    {
        return std::unexpected(AiQuickMessageStoreError::invalidFormat);
    }
    QJsonArray values;
    for (const AiQuickMessage &message : messages)
    {
        values.append(serializeMessage(message));
    }
    const QByteArray data =
        QJsonDocument(QJsonObject{{QStringLiteral("version"), schemaVersion}, {QStringLiteral("messages"), values}})
            .toJson(QJsonDocument::Indented);
    if (data.size() > maximumFileBytes)
    {
        return std::unexpected(AiQuickMessageStoreError::invalidFormat);
    }
    const QFileInfo info(m_filePath);
    if (!info.absoluteDir().mkpath(QStringLiteral(".")))
    {
        return std::unexpected(AiQuickMessageStoreError::ioError);
    }
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    {
        file.cancelWriting();
        return std::unexpected(AiQuickMessageStoreError::ioError);
    }
    return {};
}

} // namespace ztermy::ai
