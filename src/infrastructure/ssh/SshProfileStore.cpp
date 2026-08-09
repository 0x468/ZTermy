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
constexpr qint64 keywordSchemaVersion = 3;
constexpr qint64 sessionOptionsSchemaVersion = 4;
constexpr qint64 currentSchemaVersion = 5;
constexpr qsizetype maximumEnvironmentVariableCount = 32;

[[nodiscard]] std::optional<ztermy::ssh::SshStartupCommandMode> parseStartupCommandMode(const QString &value)
{
    if (value == QStringLiteral("paste"))
    {
        return ztermy::ssh::SshStartupCommandMode::Paste;
    }
    if (value == QStringLiteral("line-delay"))
    {
        return ztermy::ssh::SshStartupCommandMode::LineDelay;
    }
    return std::nullopt;
}

[[nodiscard]] QString serializeStartupCommandMode(const ztermy::ssh::SshStartupCommandMode mode)
{
    return mode == ztermy::ssh::SshStartupCommandMode::LineDelay ? QStringLiteral("line-delay")
                                                                 : QStringLiteral("paste");
}

[[nodiscard]] std::optional<ztermy::ssh::SshReconnectPolicy> parseReconnectPolicy(const QString &value)
{
    if (value == QStringLiteral("never"))
    {
        return ztermy::ssh::SshReconnectPolicy::Never;
    }
    if (value == QStringLiteral("transport-failure"))
    {
        return ztermy::ssh::SshReconnectPolicy::OnTransportFailure;
    }
    return std::nullopt;
}

[[nodiscard]] QString serializeReconnectPolicy(const ztermy::ssh::SshReconnectPolicy policy)
{
    return policy == ztermy::ssh::SshReconnectPolicy::OnTransportFailure ? QStringLiteral("transport-failure")
                                                                         : QStringLiteral("never");
}

[[nodiscard]] std::optional<ztermy::ssh::SshProxyType> parseProxyType(const QString &value)
{
    if (value == QStringLiteral("none"))
    {
        return ztermy::ssh::SshProxyType::None;
    }
    if (value == QStringLiteral("socks5"))
    {
        return ztermy::ssh::SshProxyType::Socks5;
    }
    if (value == QStringLiteral("http-connect"))
    {
        return ztermy::ssh::SshProxyType::HttpConnect;
    }
    return std::nullopt;
}

[[nodiscard]] QString serializeProxyType(const ztermy::ssh::SshProxyType type)
{
    switch (type)
    {
        case ztermy::ssh::SshProxyType::None:
            return QStringLiteral("none");
        case ztermy::ssh::SshProxyType::Socks5:
            return QStringLiteral("socks5");
        case ztermy::ssh::SshProxyType::HttpConnect:
            return QStringLiteral("http-connect");
    }
    return {};
}

[[nodiscard]] std::optional<ztermy::ssh::SshProxyOptions> parseProxyOptions(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue typeValue = object.value(QStringLiteral("type"));
    const QJsonValue hostValue = object.value(QStringLiteral("host"));
    const QJsonValue portValue = object.value(QStringLiteral("port"));
    const QJsonValue usernameValue = object.value(QStringLiteral("username"));
    const QJsonValue credentialReferenceValue = object.value(QStringLiteral("credentialReference"));
    if (!typeValue.isString() || !hostValue.isString() || !portValue.isDouble() || !usernameValue.isString()
        || (!credentialReferenceValue.isUndefined() && !credentialReferenceValue.isString()))
    {
        return std::nullopt;
    }
    const auto type = parseProxyType(typeValue.toString());
    const qint64 port = portValue.toInteger(-1);
    if (!type || static_cast<double>(port) != portValue.toDouble() || port < 0
        || std::cmp_greater(port, std::numeric_limits<std::uint16_t>::max()))
    {
        return std::nullopt;
    }
    ztermy::ssh::SshProxyOptions options{
        .type = *type,
        .host = hostValue.toString().toStdString(),
        .port = static_cast<std::uint16_t>(port),
        .username = usernameValue.toString().toStdString(),
        .credentialReference = credentialReferenceValue.isString()
                                   ? std::optional{credentialReferenceValue.toString().toStdString()}
                                   : std::nullopt,
    };
    return ztermy::ssh::validSshProxyOptions(options) ? std::optional{std::move(options)} : std::nullopt;
}

[[nodiscard]] QJsonObject serializeProxyOptions(const ztermy::ssh::SshProxyOptions &options)
{
    QJsonObject object{
        {QStringLiteral("type"), serializeProxyType(options.type)},
        {QStringLiteral("host"), QString::fromStdString(options.host)},
        {QStringLiteral("port"), options.port},
        {QStringLiteral("username"), QString::fromStdString(options.username)},
    };
    if (options.credentialReference)
    {
        object.insert(QStringLiteral("credentialReference"), QString::fromStdString(*options.credentialReference));
    }
    return object;
}

[[nodiscard]] std::optional<ztermy::ssh::SshSessionOptions> parseSessionOptions(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue terminalType = object.value(QStringLiteral("terminalType"));
    const QJsonValue keepaliveInterval = object.value(QStringLiteral("keepaliveIntervalSeconds"));
    const QJsonValue keepaliveThreshold = object.value(QStringLiteral("keepaliveFailureThreshold"));
    const QJsonValue startupCommand = object.value(QStringLiteral("startupCommand"));
    const QJsonValue startupMode = object.value(QStringLiteral("startupCommandMode"));
    const QJsonValue startupDelay = object.value(QStringLiteral("startupLineDelayMilliseconds"));
    const QJsonValue environment = object.value(QStringLiteral("environment"));
    const QJsonValue reconnectPolicy = object.value(QStringLiteral("reconnectPolicy"));
    const QJsonValue reconnectAttempts = object.value(QStringLiteral("reconnectMaximumAttempts"));
    const QJsonValue reconnectBackoff = object.value(QStringLiteral("reconnectInitialBackoffMilliseconds"));
    if (!terminalType.isString() || !keepaliveInterval.isDouble() || !keepaliveThreshold.isDouble()
        || !startupCommand.isString() || !startupMode.isString() || !startupDelay.isDouble() || !environment.isArray()
        || !reconnectPolicy.isString() || !reconnectAttempts.isDouble() || !reconnectBackoff.isDouble())
    {
        return std::nullopt;
    }
    const auto parsedStartupMode = parseStartupCommandMode(startupMode.toString());
    const auto parsedReconnectPolicy = parseReconnectPolicy(reconnectPolicy.toString());
    const qint64 interval = keepaliveInterval.toInteger(-1);
    const qint64 threshold = keepaliveThreshold.toInteger(-1);
    const qint64 delay = startupDelay.toInteger(-1);
    const qint64 attempts = reconnectAttempts.toInteger(-1);
    const qint64 backoff = reconnectBackoff.toInteger(-1);
    if (!parsedStartupMode || !parsedReconnectPolicy || interval < 0 || threshold < 0 || delay < 0 || attempts < 0
        || backoff < 0 || static_cast<double>(interval) != keepaliveInterval.toDouble()
        || static_cast<double>(threshold) != keepaliveThreshold.toDouble()
        || static_cast<double>(delay) != startupDelay.toDouble()
        || static_cast<double>(attempts) != reconnectAttempts.toDouble()
        || static_cast<double>(backoff) != reconnectBackoff.toDouble()
        || std::cmp_greater(interval, std::numeric_limits<std::uint16_t>::max())
        || std::cmp_greater(threshold, std::numeric_limits<std::uint8_t>::max())
        || std::cmp_greater(delay, std::numeric_limits<std::uint16_t>::max())
        || std::cmp_greater(attempts, std::numeric_limits<std::uint8_t>::max())
        || std::cmp_greater(backoff, std::numeric_limits<std::uint16_t>::max()))
    {
        return std::nullopt;
    }

    std::vector<ztermy::ssh::SshEnvironmentVariable> variables;
    const QJsonArray values = environment.toArray();
    if (values.size() > maximumEnvironmentVariableCount)
    {
        return std::nullopt;
    }
    variables.reserve(static_cast<std::size_t>(values.size()));
    for (const auto variableValue : values)
    {
        if (!variableValue.isObject())
        {
            return std::nullopt;
        }
        const QJsonObject variableObject = variableValue.toObject();
        const QJsonValue name = variableObject.value(QStringLiteral("name"));
        const QJsonValue variableContent = variableObject.value(QStringLiteral("value"));
        if (!name.isString() || !variableContent.isString())
        {
            return std::nullopt;
        }
        variables.push_back({.name = name.toString().toStdString(), .value = variableContent.toString().toStdString()});
    }

    ztermy::ssh::SshSessionOptions options{
        .terminalType = terminalType.toString().toStdString(),
        .keepaliveIntervalSeconds = static_cast<std::uint16_t>(interval),
        .keepaliveFailureThreshold = static_cast<std::uint8_t>(threshold),
        .startupCommand = startupCommand.toString().toStdString(),
        .startupCommandMode = *parsedStartupMode,
        .startupLineDelayMilliseconds = static_cast<std::uint16_t>(delay),
        .environment = std::move(variables),
        .reconnectPolicy = *parsedReconnectPolicy,
        .reconnectMaximumAttempts = static_cast<std::uint8_t>(attempts),
        .reconnectInitialBackoffMilliseconds = static_cast<std::uint16_t>(backoff),
    };
    return ztermy::ssh::validSshSessionOptions(options) ? std::optional{std::move(options)} : std::nullopt;
}

[[nodiscard]] QJsonObject serializeSessionOptions(const ztermy::ssh::SshSessionOptions &options)
{
    QJsonArray environment;
    for (const ztermy::ssh::SshEnvironmentVariable &variable : options.environment)
    {
        environment.append(QJsonObject{{QStringLiteral("name"), QString::fromStdString(variable.name)},
                                       {QStringLiteral("value"), QString::fromStdString(variable.value)}});
    }
    return {
        {QStringLiteral("terminalType"), QString::fromStdString(options.terminalType)},
        {QStringLiteral("keepaliveIntervalSeconds"), options.keepaliveIntervalSeconds},
        {QStringLiteral("keepaliveFailureThreshold"), options.keepaliveFailureThreshold},
        {QStringLiteral("startupCommand"), QString::fromStdString(options.startupCommand)},
        {QStringLiteral("startupCommandMode"), serializeStartupCommandMode(options.startupCommandMode)},
        {QStringLiteral("startupLineDelayMilliseconds"), options.startupLineDelayMilliseconds},
        {QStringLiteral("environment"), environment},
        {QStringLiteral("reconnectPolicy"), serializeReconnectPolicy(options.reconnectPolicy)},
        {QStringLiteral("reconnectMaximumAttempts"), options.reconnectMaximumAttempts},
        {QStringLiteral("reconnectInitialBackoffMilliseconds"), options.reconnectInitialBackoffMilliseconds},
    };
}

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
    if (value == QStringLiteral("agent"))
    {
        return ztermy::ssh::SshAuthenticationMethod::Agent;
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
        case ztermy::ssh::SshAuthenticationMethod::Agent:
            return QStringLiteral("agent");
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
    const QJsonValue sessionOptionsValue = object.value(QStringLiteral("sessionOptions"));
    const QJsonValue proxyValue = object.value(QStringLiteral("proxy"));
    if (!idValue.isString() || !nameValue.isString() || (!groupValue.isUndefined() && !groupValue.isString())
        || !hostValue.isString() || !portValue.isDouble() || !usernameValue.isString()
        || !authenticationValue.isString() || !privateKeyPathValue.isString()
        || (!passphraseRequiredValue.isUndefined() && !passphraseRequiredValue.isBool())
        || (!lastConnectedValue.isUndefined() && !lastConnectedValue.isDouble())
        || (version >= credentialSchemaVersion && !credentialReferenceValue.isUndefined()
            && !credentialReferenceValue.isString())
        || (version >= keywordSchemaVersion && !keywordEnabledValue.isUndefined() && !keywordEnabledValue.isBool())
        || (version >= keywordSchemaVersion && !keywordRulesValue.isUndefined() && !keywordRulesValue.isArray())
        || (version >= sessionOptionsSchemaVersion && !sessionOptionsValue.isObject())
        || (version >= currentSchemaVersion && !proxyValue.isObject()))
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

    auto sessionOptions = version >= sessionOptionsSchemaVersion ? parseSessionOptions(sessionOptionsValue)
                                                                 : std::optional{ztermy::ssh::SshSessionOptions{}};
    if (!sessionOptions)
    {
        return std::nullopt;
    }
    auto proxy =
        version >= currentSchemaVersion ? parseProxyOptions(proxyValue) : std::optional{ztermy::ssh::SshProxyOptions{}};
    if (!proxy)
    {
        return std::nullopt;
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
        .sessionOptions = std::move(*sessionOptions),
        .proxy = std::move(*proxy),
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
    object.insert(QStringLiteral("sessionOptions"), serializeSessionOptions(profile.sessionOptions));
    object.insert(QStringLiteral("proxy"), serializeProxyOptions(profile.proxy));
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
            && versionValue.toInteger() != keywordSchemaVersion
            && versionValue.toInteger() != sessionOptionsSchemaVersion
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
