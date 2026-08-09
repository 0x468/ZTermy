#include "infrastructure/forwarding/PortForwardingRuleStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <limits>
#include <optional>
#include <utility>

namespace ztermy::forwarding
{
namespace
{

constexpr qint64 currentSchemaVersion = 1;
constexpr qint64 maximumFileSize = qint64{512} * 1024;

[[nodiscard]] QString typeName(const PortForwardingType type)
{
    switch (type)
    {
        case PortForwardingType::Local:
            return QStringLiteral("local");
        case PortForwardingType::Remote:
            return QStringLiteral("remote");
        case PortForwardingType::Dynamic:
            return QStringLiteral("dynamic");
    }
    return {};
}

[[nodiscard]] std::optional<PortForwardingType> parseType(const QString &value)
{
    if (value == QStringLiteral("local"))
    {
        return PortForwardingType::Local;
    }
    if (value == QStringLiteral("remote"))
    {
        return PortForwardingType::Remote;
    }
    if (value == QStringLiteral("dynamic"))
    {
        return PortForwardingType::Dynamic;
    }
    return std::nullopt;
}

[[nodiscard]] QJsonObject serializeEndpoint(const PortForwardingEndpoint &endpoint)
{
    return {
        {QStringLiteral("host"), QString::fromStdString(endpoint.host)},
        {QStringLiteral("port"), endpoint.port},
    };
}

[[nodiscard]] std::optional<PortForwardingEndpoint> parseEndpoint(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue hostValue = object.value(QStringLiteral("host"));
    const QJsonValue portValue = object.value(QStringLiteral("port"));
    if (!hostValue.isString() || !portValue.isDouble())
    {
        return std::nullopt;
    }
    const qint64 port = portValue.toInteger(-1);
    if (!std::in_range<std::uint16_t>(port))
    {
        return std::nullopt;
    }
    return PortForwardingEndpoint{.host = hostValue.toString().toStdString(), .port = static_cast<std::uint16_t>(port)};
}

[[nodiscard]] QJsonObject serializeRule(const PortForwardingRule &rule)
{
    QJsonObject object{
        {QStringLiteral("id"), QString::fromStdString(rule.id)},
        {QStringLiteral("label"), QString::fromStdString(rule.label)},
        {QStringLiteral("profileId"), QString::fromStdString(rule.profileId)},
        {QStringLiteral("type"), typeName(rule.type)},
        {QStringLiteral("bind"), serializeEndpoint(rule.bind)},
        {QStringLiteral("autoStart"), rule.autoStart},
    };
    if (rule.type != PortForwardingType::Dynamic)
    {
        object.insert(QStringLiteral("destination"), serializeEndpoint(rule.destination));
    }
    return object;
}

[[nodiscard]] std::optional<PortForwardingRule> parseRule(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue id = object.value(QStringLiteral("id"));
    const QJsonValue label = object.value(QStringLiteral("label"));
    const QJsonValue profileId = object.value(QStringLiteral("profileId"));
    const QJsonValue typeValue = object.value(QStringLiteral("type"));
    const QJsonValue autoStart = object.value(QStringLiteral("autoStart"));
    if (!id.isString() || !label.isString() || !profileId.isString() || !typeValue.isString() || !autoStart.isBool())
    {
        return std::nullopt;
    }
    const auto type = parseType(typeValue.toString());
    const auto bind = parseEndpoint(object.value(QStringLiteral("bind")));
    if (!type || !bind)
    {
        return std::nullopt;
    }
    PortForwardingEndpoint destination;
    if (*type != PortForwardingType::Dynamic)
    {
        const auto parsed = parseEndpoint(object.value(QStringLiteral("destination")));
        if (!parsed)
        {
            return std::nullopt;
        }
        destination = *parsed;
    }

    PortForwardingRule rule{
        .id = id.toString().toStdString(),
        .label = label.toString().toStdString(),
        .profileId = profileId.toString().toStdString(),
        .type = *type,
        .bind = *bind,
        .destination = std::move(destination),
        .autoStart = autoStart.toBool(),
    };
    return validPortForwardingRule(rule) ? std::optional<PortForwardingRule>{std::move(rule)} : std::nullopt;
}

} // namespace

PortForwardingRuleStore::PortForwardingRuleStore(QString filePath) : m_filePath(std::move(filePath)) {}

QString PortForwardingRuleStore::filePath() const
{
    return m_filePath;
}

std::expected<std::vector<PortForwardingRule>, PortForwardingRuleStoreError> PortForwardingRuleStore::load() const
{
    if (m_filePath.isEmpty())
    {
        return std::unexpected(PortForwardingRuleStoreError::InvalidPath);
    }
    QFile file(m_filePath);
    if (!file.exists())
    {
        return std::vector<PortForwardingRule>{};
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumFileSize)
    {
        return std::unexpected(PortForwardingRuleStoreError::Io);
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(PortForwardingRuleStoreError::InvalidDocument);
    }
    const QJsonObject root = document.object();
    const QJsonValue version = root.value(QStringLiteral("schemaVersion"));
    const QJsonValue rulesValue = root.value(QStringLiteral("rules"));
    if (!version.isDouble() || version.toInteger(-1) != currentSchemaVersion)
    {
        return std::unexpected(PortForwardingRuleStoreError::UnsupportedVersion);
    }
    if (!rulesValue.isArray())
    {
        return std::unexpected(PortForwardingRuleStoreError::InvalidDocument);
    }
    const QJsonArray values = rulesValue.toArray();
    if (values.size() > static_cast<qsizetype>(maximumPortForwardingRuleCount))
    {
        return std::unexpected(PortForwardingRuleStoreError::InvalidDocument);
    }
    std::vector<PortForwardingRule> rules;
    rules.reserve(static_cast<std::size_t>(values.size()));
    for (const auto &value : values)
    {
        auto rule = parseRule(value);
        if (!rule)
        {
            return std::unexpected(PortForwardingRuleStoreError::InvalidDocument);
        }
        rules.push_back(std::move(*rule));
    }
    if (!validPortForwardingRules(rules))
    {
        return std::unexpected(PortForwardingRuleStoreError::InvalidDocument);
    }
    return rules;
}

std::expected<void, PortForwardingRuleStoreError>
PortForwardingRuleStore::save(const std::span<const PortForwardingRule> rules) const
{
    if (m_filePath.isEmpty())
    {
        return std::unexpected(PortForwardingRuleStoreError::InvalidPath);
    }
    if (!validPortForwardingRules(rules))
    {
        return std::unexpected(PortForwardingRuleStoreError::InvalidDocument);
    }
    const QFileInfo info(m_filePath);
    QDir directory = info.absoluteDir();
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
    {
        return std::unexpected(PortForwardingRuleStoreError::Io);
    }

    QJsonArray values;
    for (const PortForwardingRule &rule : rules)
    {
        values.append(serializeRule(rule));
    }
    const QJsonDocument document(
        QJsonObject{{QStringLiteral("schemaVersion"), currentSchemaVersion}, {QStringLiteral("rules"), values}});
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        return std::unexpected(PortForwardingRuleStoreError::Io);
    }
    const QByteArray payload = document.toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size() || !file.commit())
    {
        file.cancelWriting();
        return std::unexpected(PortForwardingRuleStoreError::Io);
    }
    return {};
}

} // namespace ztermy::forwarding
