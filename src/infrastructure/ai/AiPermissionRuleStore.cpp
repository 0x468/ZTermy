#include "infrastructure/ai/AiPermissionRuleStore.h"

#include "core/persistence/LastKnownGoodFile.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <optional>
#include <unordered_set>
#include <utility>

namespace ztermy::ai
{
namespace
{
constexpr qsizetype maximumFileBytes = qsizetype{512} * 1024;

[[nodiscard]] QString capabilityToken(const AiPermissionCapability capability)
{
    switch (capability)
    {
        case AiPermissionCapability::terminalCommand:
            return QStringLiteral("terminal-command");
        case AiPermissionCapability::ptyInput:
            return QStringLiteral("pty-input");
        case AiPermissionCapability::terminalInterrupt:
            return QStringLiteral("terminal-interrupt");
        case AiPermissionCapability::runbookMutation:
            return QStringLiteral("runbook-mutation");
        case AiPermissionCapability::sftpDownload:
            return QStringLiteral("sftp-download");
        case AiPermissionCapability::sftpUpload:
            return QStringLiteral("sftp-upload");
        case AiPermissionCapability::mcpTool:
            return QStringLiteral("mcp-tool");
    }
    return QStringLiteral("terminal-command");
}

[[nodiscard]] std::optional<AiPermissionCapability> parseCapability(const QString &token)
{
    if (token == QStringLiteral("terminal-command"))
        return AiPermissionCapability::terminalCommand;
    if (token == QStringLiteral("pty-input"))
        return AiPermissionCapability::ptyInput;
    if (token == QStringLiteral("terminal-interrupt"))
        return AiPermissionCapability::terminalInterrupt;
    if (token == QStringLiteral("runbook-mutation"))
        return AiPermissionCapability::runbookMutation;
    if (token == QStringLiteral("sftp-download"))
        return AiPermissionCapability::sftpDownload;
    if (token == QStringLiteral("sftp-upload"))
        return AiPermissionCapability::sftpUpload;
    if (token == QStringLiteral("mcp-tool"))
        return AiPermissionCapability::mcpTool;
    return std::nullopt;
}

[[nodiscard]] QString matcherToken(const AiPermissionRuleMatcher matcher)
{
    switch (matcher)
    {
        case AiPermissionRuleMatcher::exact:
            return QStringLiteral("exact");
        case AiPermissionRuleMatcher::prefix:
            return QStringLiteral("prefix");
        case AiPermissionRuleMatcher::glob:
            return QStringLiteral("glob");
        case AiPermissionRuleMatcher::regex:
            return QStringLiteral("regex");
        case AiPermissionRuleMatcher::all:
            return QStringLiteral("all");
    }
    return QStringLiteral("exact");
}

[[nodiscard]] std::optional<AiPermissionRuleMatcher> parseMatcher(const QString &token)
{
    if (token == QStringLiteral("exact"))
        return AiPermissionRuleMatcher::exact;
    if (token == QStringLiteral("prefix"))
        return AiPermissionRuleMatcher::prefix;
    if (token == QStringLiteral("glob"))
        return AiPermissionRuleMatcher::glob;
    if (token == QStringLiteral("regex"))
        return AiPermissionRuleMatcher::regex;
    if (token == QStringLiteral("all"))
        return AiPermissionRuleMatcher::all;
    return std::nullopt;
}

[[nodiscard]] QString dispositionToken(const AiPermissionDisposition disposition)
{
    switch (disposition)
    {
        case AiPermissionDisposition::allow:
            return QStringLiteral("allow");
        case AiPermissionDisposition::ask:
            return QStringLiteral("ask");
        case AiPermissionDisposition::deny:
            return QStringLiteral("deny");
    }
    return QStringLiteral("ask");
}

[[nodiscard]] std::optional<AiPermissionDisposition> parseDisposition(const QString &token)
{
    if (token == QStringLiteral("allow"))
        return AiPermissionDisposition::allow;
    if (token == QStringLiteral("ask"))
        return AiPermissionDisposition::ask;
    if (token == QStringLiteral("deny"))
        return AiPermissionDisposition::deny;
    return std::nullopt;
}

[[nodiscard]] QString durationToken(const AiPermissionRuleDuration duration)
{
    return duration == AiPermissionRuleDuration::profile ? QStringLiteral("profile") : QStringLiteral("global");
}

[[nodiscard]] std::optional<AiPermissionRuleDuration> parseDuration(const QString &token)
{
    if (token == QStringLiteral("profile"))
        return AiPermissionRuleDuration::profile;
    if (token == QStringLiteral("global"))
        return AiPermissionRuleDuration::global;
    return std::nullopt;
}

[[nodiscard]] std::expected<std::vector<AiPermissionRule>, QString> parse(const QByteArrayView bytes)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(bytes.data(), bytes.size()), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(QStringLiteral("The AI permission rule configuration has an invalid format."));
    }
    const QJsonObject root = document.object();
    if (!root.value(QStringLiteral("schema_version")).isDouble()
        || root.value(QStringLiteral("schema_version")).toInt() != 1)
    {
        return std::unexpected(QStringLiteral("The AI permission rule configuration has an unsupported version."));
    }
    const QJsonArray values = root.value(QStringLiteral("rules")).toArray();
    if (values.size() > static_cast<qsizetype>(AiPermissionRuleEngine::maximumRules))
    {
        return std::unexpected(QStringLiteral("The AI permission rule configuration exceeds the rule limit."));
    }
    std::vector<AiPermissionRule> rules;
    rules.reserve(static_cast<std::size_t>(values.size()));
    std::unordered_set<std::string> ids;
    for (const auto &value : values)
    {
        const QJsonObject object = value.toObject();
        const auto capability = parseCapability(object.value(QStringLiteral("capability")).toString());
        const auto matcher = parseMatcher(object.value(QStringLiteral("matcher")).toString());
        const auto disposition = parseDisposition(object.value(QStringLiteral("decision")).toString());
        const auto duration = parseDuration(object.value(QStringLiteral("duration")).toString());
        AiPermissionRule rule{.id = object.value(QStringLiteral("id")).toString().toStdString(),
                              .capability = capability.value_or(AiPermissionCapability::terminalCommand),
                              .matcher = matcher.value_or(AiPermissionRuleMatcher::exact),
                              .pattern = object.value(QStringLiteral("pattern")).toString().toStdString(),
                              .disposition = disposition.value_or(AiPermissionDisposition::ask),
                              .duration = duration.value_or(AiPermissionRuleDuration::once),
                              .profileId = object.value(QStringLiteral("profile_id")).toString().toStdString(),
                              .enabled = object.value(QStringLiteral("enabled")).toBool(true)};
        if (!value.isObject() || !capability.has_value() || !matcher.has_value() || !disposition.has_value()
            || !duration.has_value() || !AiPermissionRuleEngine::valid(rule) || !ids.insert(rule.id).second)
        {
            return std::unexpected(QStringLiteral("An AI permission rule is invalid or duplicated."));
        }
        rules.push_back(std::move(rule));
    }
    return rules;
}

[[nodiscard]] persistence::PayloadValidation validate(const QByteArrayView bytes)
{
    const auto parsed = parse(bytes);
    if (parsed.has_value())
        return persistence::PayloadValidation::valid;
    return parsed.error().contains(QStringLiteral("unsupported version"))
               ? persistence::PayloadValidation::unsupportedVersion
               : persistence::PayloadValidation::invalid;
}

[[nodiscard]] QString persistenceError(const persistence::LastKnownGoodError error)
{
    switch (error)
    {
        case persistence::LastKnownGoodError::invalidPath:
            return QStringLiteral("The AI permission rule path is invalid.");
        case persistence::LastKnownGoodError::io:
            return QStringLiteral("The AI permission rules could not be read or written.");
        case persistence::LastKnownGoodError::invalidFormat:
            return QStringLiteral("The AI permission rules and their backup are invalid.");
        case persistence::LastKnownGoodError::unsupportedVersion:
            return QStringLiteral("The AI permission rule version is unsupported.");
    }
    return QStringLiteral("The AI permission rule operation failed.");
}
} // namespace

AiPermissionRuleStore::AiPermissionRuleStore(QString filePath) : m_filePath(std::move(filePath)) {}

const QString &AiPermissionRuleStore::filePath() const noexcept
{
    return m_filePath;
}

std::expected<std::vector<AiPermissionRule>, QString> AiPermissionRuleStore::load()
{
    m_lastLoadRecoveredFromBackup = false;
    auto payload = persistence::loadLastKnownGood(m_filePath, maximumFileBytes, validate);
    if (!payload.has_value())
        return std::unexpected(persistenceError(payload.error()));
    if (!payload->has_value())
        return std::vector<AiPermissionRule>{};
    m_lastLoadRecoveredFromBackup = payload->value().recoveredFromBackup;
    return parse(payload->value().bytes);
}

std::expected<void, QString> AiPermissionRuleStore::save(const std::vector<AiPermissionRule> &rules) const
{
    if (rules.size() > AiPermissionRuleEngine::maximumRules)
        return std::unexpected(QStringLiteral("Too many AI permission rules are configured."));
    QJsonArray values;
    for (const auto &rule : rules)
    {
        if ((rule.duration != AiPermissionRuleDuration::profile && rule.duration != AiPermissionRuleDuration::global)
            || !AiPermissionRuleEngine::valid(rule))
        {
            return std::unexpected(QStringLiteral("Only valid profile and global AI permission rules can be saved."));
        }
        values.push_back(QJsonObject{{QStringLiteral("id"), QString::fromStdString(rule.id)},
                                     {QStringLiteral("capability"), capabilityToken(rule.capability)},
                                     {QStringLiteral("matcher"), matcherToken(rule.matcher)},
                                     {QStringLiteral("pattern"), QString::fromStdString(rule.pattern)},
                                     {QStringLiteral("decision"), dispositionToken(rule.disposition)},
                                     {QStringLiteral("duration"), durationToken(rule.duration)},
                                     {QStringLiteral("profile_id"), QString::fromStdString(rule.profileId)},
                                     {QStringLiteral("enabled"), rule.enabled}});
    }
    const QByteArray bytes =
        QJsonDocument(QJsonObject{{QStringLiteral("schema_version"), 1}, {QStringLiteral("rules"), values}})
            .toJson(QJsonDocument::Indented);
    const auto saved = persistence::saveLastKnownGood(m_filePath, bytes, maximumFileBytes, validate);
    return saved.has_value() ? std::expected<void, QString>{} : std::unexpected(persistenceError(saved.error()));
}

bool AiPermissionRuleStore::lastLoadRecoveredFromBackup() const noexcept
{
    return m_lastLoadRecoveredFromBackup;
}

} // namespace ztermy::ai
