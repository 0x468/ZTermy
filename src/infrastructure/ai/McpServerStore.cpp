#include "infrastructure/ai/McpServerStore.h"

#include "core/persistence/LastKnownGoodFile.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

#include <algorithm>
#include <optional>
#include <unordered_set>

namespace ztermy::ai
{
namespace
{
constexpr qsizetype maximumFileBytes = 256 * 1024;
constexpr std::size_t maximumServers = 8;
constexpr qsizetype maximumArguments = 32;
constexpr qsizetype maximumApprovedTools = 128;

[[nodiscard]] QString trustToken(const McpServerTrust trust)
{
    switch (trust)
    {
        case McpServerTrust::disabled:
            return QStringLiteral("disabled");
        case McpServerTrust::observe:
            return QStringLiteral("observe");
        case McpServerTrust::execute:
            return QStringLiteral("execute");
    }
    return QStringLiteral("disabled");
}

[[nodiscard]] std::optional<McpServerTrust> parseTrust(const QString &token)
{
    if (token == QStringLiteral("disabled"))
    {
        return McpServerTrust::disabled;
    }
    if (token == QStringLiteral("observe"))
    {
        return McpServerTrust::observe;
    }
    if (token == QStringLiteral("execute"))
    {
        return McpServerTrust::execute;
    }
    return std::nullopt;
}

[[nodiscard]] bool validIdentityToken(const QString &value, const bool nameSpace)
{
    static const QRegularExpression idPattern(QStringLiteral("^[a-z0-9][a-z0-9._-]{0,31}$"));
    static const QRegularExpression namespacePattern(QStringLiteral("^[a-z][a-z0-9_]{0,31}$"));
    return (nameSpace ? namespacePattern : idPattern).match(value).hasMatch();
}

[[nodiscard]] std::expected<std::vector<McpServerRecord>, QString> parse(const QByteArrayView bytes)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(bytes.data(), bytes.size()), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(QStringLiteral("The MCP server configuration has an invalid format."));
    }
    const QJsonObject root = document.object();
    if (!root.value(QStringLiteral("schema_version")).isDouble()
        || root.value(QStringLiteral("schema_version")).toInt() != 1)
    {
        return std::unexpected(QStringLiteral("The MCP server configuration has an unsupported version."));
    }
    const QJsonArray values = root.value(QStringLiteral("servers")).toArray();
    if (values.size() > static_cast<qsizetype>(maximumServers))
    {
        return std::unexpected(QStringLiteral("The MCP server configuration exceeds the server limit."));
    }
    std::vector<McpServerRecord> servers;
    servers.reserve(static_cast<std::size_t>(values.size()));
    std::unordered_set<std::string> ids;
    std::unordered_set<std::string> namespaces;
    for (const QJsonValue &value : values)
    {
        const QJsonObject object = value.toObject();
        const QString id = object.value(QStringLiteral("id")).toString();
        const QString nameSpace = object.value(QStringLiteral("namespace")).toString();
        const QString program = object.value(QStringLiteral("program")).toString();
        const auto trust = parseTrust(object.value(QStringLiteral("trust")).toString());
        const QJsonArray arguments = object.value(QStringLiteral("arguments")).toArray();
        const QJsonArray approved = object.value(QStringLiteral("approved_tools")).toArray();
        if (!validIdentityToken(id, false) || !validIdentityToken(nameSpace, true) || program.trimmed().isEmpty()
            || program.size() > 1024 || !trust.has_value() || arguments.size() > maximumArguments
            || approved.size() > maximumApprovedTools || !ids.insert(id.toStdString()).second
            || !namespaces.insert(nameSpace.toStdString()).second)
        {
            return std::unexpected(QStringLiteral("An MCP server configuration is invalid or duplicated."));
        }
        McpServerRecord record;
        record.configuration.identity = {.id = id.toStdString(), .nameSpace = nameSpace.toStdString(), .trust = *trust};
        record.configuration.program = program;
        record.configuration.workingDirectory = object.value(QStringLiteral("working_directory")).toString();
        record.enabled = object.value(QStringLiteral("enabled")).toBool(false);
        for (const QJsonValue &argument : arguments)
        {
            const QString text = argument.toString();
            if (!argument.isString() || text.size() > 2048)
            {
                return std::unexpected(QStringLiteral("An MCP server argument is invalid."));
            }
            record.configuration.arguments.push_back(text);
        }
        std::unordered_set<std::string> approvedNames;
        for (const QJsonValue &approval : approved)
        {
            const QJsonObject item = approval.toObject();
            const QString name = item.value(QStringLiteral("name")).toString();
            const QString digest = item.value(QStringLiteral("schema_digest")).toString();
            if (name.isEmpty() || name.size() > 128 || digest.size() != 64
                || !approvedNames.insert(name.toStdString()).second)
            {
                return std::unexpected(QStringLiteral("An MCP tool approval is invalid or duplicated."));
            }
            record.approvedTools.push_back({.exposedName = name.toStdString(), .schemaDigest = digest.toStdString()});
        }
        servers.push_back(std::move(record));
    }
    return servers;
}

[[nodiscard]] persistence::PayloadValidation validate(const QByteArrayView bytes)
{
    const auto parsed = parse(bytes);
    if (parsed.has_value())
    {
        return persistence::PayloadValidation::valid;
    }
    return parsed.error().contains(QStringLiteral("unsupported version"))
               ? persistence::PayloadValidation::unsupportedVersion
               : persistence::PayloadValidation::invalid;
}

[[nodiscard]] QString persistenceError(const persistence::LastKnownGoodError error)
{
    switch (error)
    {
        case persistence::LastKnownGoodError::invalidPath:
            return QStringLiteral("The MCP server configuration path is invalid.");
        case persistence::LastKnownGoodError::io:
            return QStringLiteral("The MCP server configuration could not be read or written.");
        case persistence::LastKnownGoodError::invalidFormat:
            return QStringLiteral("The MCP server configuration and its backup are invalid.");
        case persistence::LastKnownGoodError::unsupportedVersion:
            return QStringLiteral("The MCP server configuration version is unsupported.");
    }
    return QStringLiteral("The MCP server configuration operation failed.");
}
} // namespace

McpServerStore::McpServerStore(QString filePath) : m_filePath(std::move(filePath)) {}

const QString &McpServerStore::filePath() const noexcept
{
    return m_filePath;
}

std::expected<std::vector<McpServerRecord>, QString> McpServerStore::load()
{
    m_lastLoadRecoveredFromBackup = false;
    auto payload = persistence::loadLastKnownGood(m_filePath, maximumFileBytes, validate);
    if (!payload.has_value())
    {
        return std::unexpected(persistenceError(payload.error()));
    }
    if (!payload->has_value())
    {
        return std::vector<McpServerRecord>{};
    }
    m_lastLoadRecoveredFromBackup = payload->value().recoveredFromBackup;
    return parse(payload->value().bytes);
}

std::expected<void, QString> McpServerStore::save(const std::vector<McpServerRecord> &servers) const
{
    if (servers.size() > maximumServers)
    {
        return std::unexpected(QStringLiteral("Too many MCP servers are configured."));
    }
    QJsonArray values;
    for (const auto &record : servers)
    {
        QJsonArray arguments;
        for (const QString &argument : record.configuration.arguments)
        {
            arguments.push_back(argument);
        }
        QJsonArray approvals;
        for (const auto &approval : record.approvedTools)
        {
            approvals.push_back(
                QJsonObject{{QStringLiteral("name"), QString::fromStdString(approval.exposedName)},
                            {QStringLiteral("schema_digest"), QString::fromStdString(approval.schemaDigest)}});
        }
        values.push_back(
            QJsonObject{{QStringLiteral("id"), QString::fromStdString(record.configuration.identity.id)},
                        {QStringLiteral("namespace"), QString::fromStdString(record.configuration.identity.nameSpace)},
                        {QStringLiteral("program"), record.configuration.program},
                        {QStringLiteral("arguments"), arguments},
                        {QStringLiteral("working_directory"), record.configuration.workingDirectory},
                        {QStringLiteral("trust"), trustToken(record.configuration.identity.trust)},
                        {QStringLiteral("enabled"), record.enabled},
                        {QStringLiteral("approved_tools"), approvals}});
    }
    const QByteArray bytes =
        QJsonDocument(QJsonObject{{QStringLiteral("schema_version"), 1}, {QStringLiteral("servers"), values}})
            .toJson(QJsonDocument::Indented);
    if (validate(bytes) != persistence::PayloadValidation::valid)
    {
        return std::unexpected(QStringLiteral("The MCP server configuration is invalid."));
    }
    const auto saved = persistence::saveLastKnownGood(m_filePath, bytes, maximumFileBytes, validate);
    return saved.has_value() ? std::expected<void, QString>{} : std::unexpected(persistenceError(saved.error()));
}

bool McpServerStore::lastLoadRecoveredFromBackup() const noexcept
{
    return m_lastLoadRecoveredFromBackup;
}

} // namespace ztermy::ai
