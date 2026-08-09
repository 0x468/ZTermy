#include "infrastructure/ssh/SshProfileStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace
{

constexpr qint64 maximumFileSize = qint64{1024} * 1024;
constexpr qsizetype maximumProfileCount = 128;
constexpr qint64 legacySchemaVersion = 1;
constexpr qint64 credentialSchemaVersion = 2;
constexpr qint64 currentSchemaVersion = 3;

[[nodiscard]] std::optional<ztermy::ssh::SshKeywordHighlightRule> parseKeywordRule(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue id = object.value(QStringLiteral("id"));
    const QJsonValue pattern = object.value(QStringLiteral("pattern"));
    const QJsonValue foreground = object.value(QStringLiteral("foreground"));
    const QJsonValue background = object.value(QStringLiteral("background"));
    const QJsonValue enabled = object.value(QStringLiteral("enabled"));
    const QJsonValue caseSensitive = object.value(QStringLiteral("caseSensitive"));
    if (!id.isString() || !pattern.isString() || !foreground.isString() || !background.isString()
        || (!enabled.isUndefined() && !enabled.isBool()) || (!caseSensitive.isUndefined() && !caseSensitive.isBool()))
    {
        return std::nullopt;
    }
    ztermy::ssh::SshKeywordHighlightRule rule{
        .id = id.toString().toStdString(),
        .pattern = pattern.toString().toStdString(),
        .foreground = foreground.toString().toStdString(),
        .background = background.toString().toStdString(),
        .enabled = enabled.toBool(true),
        .caseSensitive = caseSensitive.toBool(false),
    };
    return ztermy::ssh::validKeywordHighlightRule(rule) ? std::optional{std::move(rule)} : std::nullopt;
}

[[nodiscard]] std::optional<ztermy::ssh::SshAuthenticationMethod> parseAuthentication(const QString &value)
{
    if (value == QStringLiteral("private-key"))
    {
        return ztermy::ssh::SshAuthenticationMethod::PrivateKey;
    }
    if (value == QStringLiteral("password"))
    {
        return ztermy::ssh::SshAuthenticationMethod::Password;
    }
    return std::nullopt;
}

[[nodiscard]] QString serializeAuthentication(const ztermy::ssh::SshAuthenticationMethod authentication)
{
    switch (authentication)
    {
        case ztermy::ssh::SshAuthenticationMethod::PrivateKey:
            return QStringLiteral("private-key");
        case ztermy::ssh::SshAuthenticationMethod::Password:
            return QStringLiteral("password");
    }
    return {};
}

[[nodiscard]] std::optional<ztermy::ssh::SshProfile> parseProfile(const QJsonValue &value, const qint64 version)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }

    const QJsonObject object = value.toObject();
    const QJsonValue idValue = object.value(QStringLiteral("id"));
    const QJsonValue nameValue = object.value(QStringLiteral("name"));
    const QJsonValue groupValue = object.value(QStringLiteral("group"));
    const QJsonValue hostValue = object.value(QStringLiteral("host"));
    const QJsonValue portValue = object.value(QStringLiteral("port"));
    const QJsonValue usernameValue = object.value(QStringLiteral("username"));
    const QJsonValue authenticationValue = object.value(QStringLiteral("authentication"));
    const QJsonValue privateKeyPathValue = object.value(QStringLiteral("privateKeyPath"));
    const QJsonValue passphraseRequiredValue = object.value(QStringLiteral("privateKeyPassphraseRequired"));
    const QJsonValue lastConnectedValue = object.value(QStringLiteral("lastConnectedUtcMs"));
    const QJsonValue credentialReferenceValue = object.value(QStringLiteral("credentialReference"));
    const QJsonValue keywordEnabledValue = object.value(QStringLiteral("keywordHighlightEnabled"));
    const QJsonValue keywordRulesValue = object.value(QStringLiteral("keywordHighlightRules"));
    if (!idValue.isString() || !nameValue.isString() || (!groupValue.isUndefined() && !groupValue.isString())
        || !hostValue.isString() || !portValue.isDouble() || !usernameValue.isString()
        || !authenticationValue.isString() || !privateKeyPathValue.isString()
        || (!passphraseRequiredValue.isUndefined() && !passphraseRequiredValue.isBool())
        || (!lastConnectedValue.isUndefined() && !lastConnectedValue.isDouble())
        || (version >= credentialSchemaVersion && !credentialReferenceValue.isUndefined()
            && !credentialReferenceValue.isString())
        || (version >= currentSchemaVersion && !keywordEnabledValue.isUndefined() && !keywordEnabledValue.isBool())
        || (version >= currentSchemaVersion && !keywordRulesValue.isUndefined() && !keywordRulesValue.isArray()))
    {
        return std::nullopt;
    }

    const qint64 port = portValue.toInteger(-1);
    if (static_cast<double>(port) != portValue.toDouble() || port <= 0
        || std::cmp_greater(port, std::numeric_limits<std::uint16_t>::max()))
    {
        return std::nullopt;
    }

    const auto authentication = parseAuthentication(authenticationValue.toString());
    if (!authentication)
    {
        return std::nullopt;
    }

    std::optional<std::int64_t> lastConnectedUtcMs;
    if (!lastConnectedValue.isUndefined())
    {
        const qint64 timestamp = lastConnectedValue.toInteger(-1);
        if (timestamp < 0 || static_cast<double>(timestamp) != lastConnectedValue.toDouble())
        {
            return std::nullopt;
        }
        lastConnectedUtcMs = timestamp;
    }

    std::vector<ztermy::ssh::SshKeywordHighlightRule> keywordRules;
    if (keywordRulesValue.isArray())
    {
        const QJsonArray values = keywordRulesValue.toArray();
        if (values.size() > 16)
        {
            return std::nullopt;
        }
        keywordRules.reserve(static_cast<std::size_t>(values.size()));
        for (const auto &ruleValue : values)
        {
            auto rule = parseKeywordRule(ruleValue);
            if (!rule)
            {
                return std::nullopt;
            }
            keywordRules.push_back(std::move(*rule));
        }
    }

    ztermy::ssh::SshProfile profile{
        .id = idValue.toString().toStdString(),
        .name = nameValue.toString().toStdString(),
        .group = groupValue.toString().toStdString(),
        .host = hostValue.toString().toStdString(),
        .port = static_cast<std::uint16_t>(port),
        .username = usernameValue.toString().toStdString(),
        .authentication = *authentication,
        .privateKeyPath = privateKeyPathValue.toString().toStdString(),
        .privateKeyPassphraseRequired = passphraseRequiredValue.toBool(false),
        .credentialReference = credentialReferenceValue.isString()
                                   ? std::optional{credentialReferenceValue.toString().toStdString()}
                                   : std::nullopt,
        .lastConnectedUtcMs = lastConnectedUtcMs,
        .keywordHighlightRules = std::move(keywordRules),
        .keywordHighlightEnabled = keywordEnabledValue.toBool(true),
    };
    return ztermy::ssh::validSshProfile(profile) ? std::optional{std::move(profile)} : std::nullopt;
}

[[nodiscard]] QJsonObject serializeProfile(const ztermy::ssh::SshProfile &profile)
{
    QJsonObject object{
        {QStringLiteral("id"), QString::fromStdString(profile.id)},
        {QStringLiteral("name"), QString::fromStdString(profile.name)},
        {QStringLiteral("group"), QString::fromStdString(profile.group)},
        {QStringLiteral("host"), QString::fromStdString(profile.host)},
        {QStringLiteral("port"), profile.port},
        {QStringLiteral("username"), QString::fromStdString(profile.username)},
        {QStringLiteral("authentication"), serializeAuthentication(profile.authentication)},
        {QStringLiteral("privateKeyPath"), QString::fromStdString(profile.privateKeyPath)},
        {QStringLiteral("privateKeyPassphraseRequired"), profile.privateKeyPassphraseRequired},
    };
    if (profile.lastConnectedUtcMs)
    {
        object.insert(QStringLiteral("lastConnectedUtcMs"), *profile.lastConnectedUtcMs);
    }
    if (profile.credentialReference)
    {
        object.insert(QStringLiteral("credentialReference"), QString::fromStdString(*profile.credentialReference));
    }
    object.insert(QStringLiteral("keywordHighlightEnabled"), profile.keywordHighlightEnabled);
    QJsonArray keywordRules;
    for (const ztermy::ssh::SshKeywordHighlightRule &rule : profile.keywordHighlightRules)
    {
        keywordRules.append(QJsonObject{
            {QStringLiteral("id"), QString::fromStdString(rule.id)},
            {QStringLiteral("pattern"), QString::fromStdString(rule.pattern)},
            {QStringLiteral("foreground"), QString::fromStdString(rule.foreground)},
            {QStringLiteral("background"), QString::fromStdString(rule.background)},
            {QStringLiteral("enabled"), rule.enabled},
            {QStringLiteral("caseSensitive"), rule.caseSensitive},
        });
    }
    object.insert(QStringLiteral("keywordHighlightRules"), keywordRules);
    return object;
}

[[nodiscard]] bool duplicateIds(const std::span<const ztermy::ssh::SshProfile> profiles)
{
    for (std::size_t index = 0; index < profiles.size(); ++index)
    {
        const auto duplicate = std::find_if(profiles.begin() + static_cast<std::ptrdiff_t>(index + 1), profiles.end(),
                                            [&profiles, index](const ztermy::ssh::SshProfile &candidate) {
                                                return candidate.id == profiles[index].id;
                                            });
        if (duplicate != profiles.end())
        {
            return true;
        }
    }
    return false;
}

} // namespace

namespace ztermy::ssh
{

SshProfileStore::SshProfileStore(QString filePath) : m_filePath(std::move(filePath)) {}

const QString &SshProfileStore::filePath() const noexcept
{
    return m_filePath;
}

std::expected<std::vector<SshProfile>, SshProfileStoreError> SshProfileStore::load() const
{
    if (m_filePath.isEmpty())
    {
        return std::unexpected(SshProfileStoreError::InvalidPath);
    }

    QFile file(m_filePath);
    if (!file.exists())
    {
        return std::vector<SshProfile>{};
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() < 0 || file.size() > maximumFileSize)
    {
        return std::unexpected(file.size() > maximumFileSize ? SshProfileStoreError::InvalidFormat
                                                             : SshProfileStoreError::IoError);
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(SshProfileStoreError::InvalidFormat);
    }

    const QJsonObject root = document.object();
    const QJsonValue versionValue = root.value(QStringLiteral("version"));
    const QJsonValue profilesValue = root.value(QStringLiteral("profiles"));
    if (!versionValue.isDouble()
        || (versionValue.toInteger() != legacySchemaVersion && versionValue.toInteger() != credentialSchemaVersion
            && versionValue.toInteger() != currentSchemaVersion))
    {
        return std::unexpected(versionValue.isDouble() ? SshProfileStoreError::UnsupportedVersion
                                                       : SshProfileStoreError::InvalidFormat);
    }
    if (!profilesValue.isArray())
    {
        return std::unexpected(SshProfileStoreError::InvalidFormat);
    }

    const QJsonArray profileValues = profilesValue.toArray();
    if (profileValues.size() > maximumProfileCount)
    {
        return std::unexpected(SshProfileStoreError::InvalidFormat);
    }

    std::vector<SshProfile> profiles;
    profiles.reserve(static_cast<std::size_t>(profileValues.size()));
    for (const auto &value : profileValues)
    {
        auto profile = parseProfile(value, versionValue.toInteger());
        if (!profile)
        {
            return std::unexpected(SshProfileStoreError::InvalidFormat);
        }
        profiles.push_back(std::move(*profile));
    }
    if (duplicateIds(profiles))
    {
        return std::unexpected(SshProfileStoreError::InvalidFormat);
    }
    return profiles;
}

std::expected<void, SshProfileStoreError> SshProfileStore::save(const std::span<const SshProfile> profiles) const
{
    if (m_filePath.isEmpty())
    {
        return std::unexpected(SshProfileStoreError::InvalidPath);
    }
    if (profiles.size() > static_cast<std::size_t>(maximumProfileCount) || duplicateIds(profiles)
        || std::ranges::any_of(profiles, [](const SshProfile &profile) {
               return !validSshProfile(profile);
           }))
    {
        return std::unexpected(SshProfileStoreError::InvalidFormat);
    }

    QJsonArray profileValues;
    for (const SshProfile &profile : profiles)
    {
        profileValues.append(serializeProfile(profile));
    }

    const QFileInfo fileInfo(m_filePath);
    if (!fileInfo.absoluteDir().mkpath(QStringLiteral(".")))
    {
        return std::unexpected(SshProfileStoreError::IoError);
    }

    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        return std::unexpected(SshProfileStoreError::IoError);
    }

    const QJsonDocument document(QJsonObject{
        {QStringLiteral("version"), currentSchemaVersion},
        {QStringLiteral("profiles"), profileValues},
    });
    const QByteArray data = document.toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit())
    {
        file.cancelWriting();
        return std::unexpected(SshProfileStoreError::IoError);
    }
    return {};
}

} // namespace ztermy::ssh
