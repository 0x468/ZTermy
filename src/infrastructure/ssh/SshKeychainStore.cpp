#include "infrastructure/ssh/SshKeychainStore.h"

#include "core/persistence/LastKnownGoodFile.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace
{

constexpr qint64 maximumFileSize = qint64{1024} * 1024;
constexpr qsizetype maximumRecordCount = 512;
constexpr qint64 currentSchemaVersion = 1;

[[nodiscard]] std::string text(const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] QString text(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::optional<ztermy::ssh::SshKeyRecord> parseKey(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue id = object.value(QStringLiteral("id"));
    const QJsonValue label = object.value(QStringLiteral("label"));
    const QJsonValue type = object.value(QStringLiteral("type"));
    const QJsonValue kind = object.value(QStringLiteral("kind"));
    const QJsonValue source = object.value(QStringLiteral("source"));
    const QJsonValue privatePath = object.value(QStringLiteral("privateKeyPath"));
    const QJsonValue publicPath = object.value(QStringLiteral("publicKeyPath"));
    const QJsonValue certificatePath = object.value(QStringLiteral("certificatePath"));
    const QJsonValue created = object.value(QStringLiteral("createdUtcMs"));
    if (!id.isString() || !label.isString() || !type.isString() || !kind.isString() || !source.isString()
        || !privatePath.isString() || !publicPath.isString() || !certificatePath.isString() || !created.isDouble())
    {
        return std::nullopt;
    }
    const QByteArray typeBytes = type.toString().toLatin1();
    const QByteArray kindBytes = kind.toString().toLatin1();
    const QByteArray sourceBytes = source.toString().toLatin1();
    const auto parsedType = ztermy::ssh::parseSshKeyType(typeBytes.toStdString());
    const auto parsedKind = ztermy::ssh::parseSshKeyKind(kindBytes.toStdString());
    const auto parsedSource = ztermy::ssh::parseSshKeySource(sourceBytes.toStdString());
    const qint64 createdValue = created.toInteger(-1);
    ztermy::ssh::SshKeyRecord record{
        .id = text(id.toString()),
        .label = text(label.toString()),
        .type = parsedType.value_or(ztermy::ssh::SshKeyType::Ed25519),
        .kind = parsedKind.value_or(ztermy::ssh::SshKeyKind::Key),
        .source = parsedSource.value_or(ztermy::ssh::SshKeySource::Reference),
        .privateKeyPath = text(privatePath.toString()),
        .publicKeyPath = text(publicPath.toString()),
        .certificatePath = text(certificatePath.toString()),
        .createdUtcMs = createdValue,
    };
    if (!parsedType || !parsedKind || !parsedSource || static_cast<double>(createdValue) != created.toDouble()
        || !ztermy::ssh::validSshKeyRecord(record))
    {
        return std::nullopt;
    }
    return record;
}

[[nodiscard]] std::optional<ztermy::ssh::SshIdentity> parseIdentity(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue id = object.value(QStringLiteral("id"));
    const QJsonValue label = object.value(QStringLiteral("label"));
    const QJsonValue username = object.value(QStringLiteral("username"));
    const QJsonValue authentication = object.value(QStringLiteral("authentication"));
    const QJsonValue credentialRequired = object.value(QStringLiteral("credentialRequired"));
    const QJsonValue created = object.value(QStringLiteral("createdUtcMs"));
    const QJsonValue keyIdValue = object.value(QStringLiteral("keyId"));
    const QJsonValue credentialValue = object.value(QStringLiteral("credentialReference"));
    if (!id.isString() || !label.isString() || !username.isString() || !authentication.isString()
        || !credentialRequired.isBool() || !created.isDouble() || (!keyIdValue.isUndefined() && !keyIdValue.isString())
        || (!credentialValue.isUndefined() && !credentialValue.isString()))
    {
        return std::nullopt;
    }
    const QByteArray authenticationBytes = authentication.toString().toLatin1();
    const auto parsedAuthentication = ztermy::ssh::parseSshIdentityAuthentication(authenticationBytes.toStdString());
    const qint64 createdValue = created.toInteger(-1);
    ztermy::ssh::SshIdentity identity{
        .id = text(id.toString()),
        .label = text(label.toString()),
        .username = text(username.toString()),
        .authentication = parsedAuthentication.value_or(ztermy::ssh::SshIdentityAuthentication::Password),
        .keyId = keyIdValue.isString() ? std::optional{text(keyIdValue.toString())} : std::nullopt,
        .credentialRequired = credentialRequired.toBool(),
        .credentialReference =
            credentialValue.isString() ? std::optional{text(credentialValue.toString())} : std::nullopt,
        .createdUtcMs = createdValue,
    };
    if (!parsedAuthentication || static_cast<double>(createdValue) != created.toDouble()
        || !ztermy::ssh::validSshIdentity(identity))
    {
        return std::nullopt;
    }
    return identity;
}

[[nodiscard]] QJsonObject serializeKey(const ztermy::ssh::SshKeyRecord &record)
{
    return {
        {QStringLiteral("id"), text(record.id)},
        {QStringLiteral("label"), text(record.label)},
        {QStringLiteral("type"), QString::fromLatin1(ztermy::ssh::sshKeyTypeToken(record.type))},
        {QStringLiteral("kind"), QString::fromLatin1(ztermy::ssh::sshKeyKindToken(record.kind))},
        {QStringLiteral("source"), QString::fromLatin1(ztermy::ssh::sshKeySourceToken(record.source))},
        {QStringLiteral("privateKeyPath"), text(record.privateKeyPath)},
        {QStringLiteral("publicKeyPath"), text(record.publicKeyPath)},
        {QStringLiteral("certificatePath"), text(record.certificatePath)},
        {QStringLiteral("createdUtcMs"), record.createdUtcMs},
    };
}

[[nodiscard]] QJsonObject serializeIdentity(const ztermy::ssh::SshIdentity &identity)
{
    QJsonObject result{
        {QStringLiteral("id"), text(identity.id)},
        {QStringLiteral("label"), text(identity.label)},
        {QStringLiteral("username"), text(identity.username)},
        {QStringLiteral("authentication"),
         QString::fromLatin1(ztermy::ssh::sshIdentityAuthenticationToken(identity.authentication))},
        {QStringLiteral("credentialRequired"), identity.credentialRequired},
        {QStringLiteral("createdUtcMs"), identity.createdUtcMs},
    };
    if (identity.keyId)
    {
        result.insert(QStringLiteral("keyId"), text(*identity.keyId));
    }
    if (identity.credentialReference)
    {
        result.insert(QStringLiteral("credentialReference"), text(*identity.credentialReference));
    }
    return result;
}

template <typename Record>
[[nodiscard]] bool duplicateIds(const std::vector<Record> &records)
{
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        if (std::ranges::find(records.begin() + static_cast<std::ptrdiff_t>(index + 1), records.end(),
                              records[index].id, &Record::id)
            != records.end())
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool validCatalog(const ztermy::ssh::SshKeychainCatalog &catalog)
{
    if (catalog.keys.size() > static_cast<std::size_t>(maximumRecordCount)
        || catalog.identities.size() > static_cast<std::size_t>(maximumRecordCount) || duplicateIds(catalog.keys)
        || duplicateIds(catalog.identities) || !std::ranges::all_of(catalog.keys, ztermy::ssh::validSshKeyRecord)
        || !std::ranges::all_of(catalog.identities, ztermy::ssh::validSshIdentity))
    {
        return false;
    }
    return std::ranges::all_of(catalog.identities, [&catalog](const ztermy::ssh::SshIdentity &identity) {
        if (!identity.keyId)
        {
            return true;
        }
        const auto key = std::ranges::find(catalog.keys, *identity.keyId, &ztermy::ssh::SshKeyRecord::id);
        if (key == catalog.keys.end())
        {
            return false;
        }
        return identity.authentication == ztermy::ssh::SshIdentityAuthentication::Certificate
                   ? key->kind == ztermy::ssh::SshKeyKind::Certificate
                   : key->kind == ztermy::ssh::SshKeyKind::Key;
    });
}

[[nodiscard]] std::expected<ztermy::ssh::SshKeychainCatalog, ztermy::ssh::SshKeychainStoreError>
parsePayload(const QByteArrayView payload)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload.toByteArray(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(ztermy::ssh::SshKeychainStoreError::InvalidFormat);
    }
    const QJsonObject root = document.object();
    const QJsonValue version = root.value(QStringLiteral("version"));
    const QJsonValue keys = root.value(QStringLiteral("keys"));
    const QJsonValue identities = root.value(QStringLiteral("identities"));
    const QJsonValue legacyProfilesMigrated = root.value(QStringLiteral("legacyProfilesMigrated"));
    if (!version.isDouble() || version.toInteger() != currentSchemaVersion)
    {
        return std::unexpected(version.isDouble() ? ztermy::ssh::SshKeychainStoreError::UnsupportedVersion
                                                  : ztermy::ssh::SshKeychainStoreError::InvalidFormat);
    }
    if (!keys.isArray() || !identities.isArray() || !legacyProfilesMigrated.isBool()
        || keys.toArray().size() > maximumRecordCount || identities.toArray().size() > maximumRecordCount)
    {
        return std::unexpected(ztermy::ssh::SshKeychainStoreError::InvalidFormat);
    }
    ztermy::ssh::SshKeychainCatalog catalog;
    catalog.legacyProfilesMigrated = legacyProfilesMigrated.toBool();
    for (const auto &value : keys.toArray())
    {
        auto record = parseKey(value);
        if (!record)
        {
            return std::unexpected(ztermy::ssh::SshKeychainStoreError::InvalidFormat);
        }
        catalog.keys.push_back(std::move(*record));
    }
    for (const auto &value : identities.toArray())
    {
        auto identity = parseIdentity(value);
        if (!identity)
        {
            return std::unexpected(ztermy::ssh::SshKeychainStoreError::InvalidFormat);
        }
        catalog.identities.push_back(std::move(*identity));
    }
    return validCatalog(catalog)
               ? std::expected<ztermy::ssh::SshKeychainCatalog, ztermy::ssh::SshKeychainStoreError>{std::move(catalog)}
               : std::unexpected(ztermy::ssh::SshKeychainStoreError::InvalidFormat);
}

[[nodiscard]] ztermy::persistence::PayloadValidation validatePayload(const QByteArrayView payload)
{
    const auto parsed = parsePayload(payload);
    if (parsed)
    {
        return ztermy::persistence::PayloadValidation::valid;
    }
    return parsed.error() == ztermy::ssh::SshKeychainStoreError::UnsupportedVersion
               ? ztermy::persistence::PayloadValidation::unsupportedVersion
               : ztermy::persistence::PayloadValidation::invalid;
}

[[nodiscard]] ztermy::ssh::SshKeychainStoreError storeError(const ztermy::persistence::LastKnownGoodError error)
{
    switch (error)
    {
        case ztermy::persistence::LastKnownGoodError::invalidPath:
            return ztermy::ssh::SshKeychainStoreError::InvalidPath;
        case ztermy::persistence::LastKnownGoodError::io:
            return ztermy::ssh::SshKeychainStoreError::IoError;
        case ztermy::persistence::LastKnownGoodError::unsupportedVersion:
            return ztermy::ssh::SshKeychainStoreError::UnsupportedVersion;
        case ztermy::persistence::LastKnownGoodError::invalidFormat:
        default:
            return ztermy::ssh::SshKeychainStoreError::InvalidFormat;
    }
}

} // namespace

namespace ztermy::ssh
{

SshKeychainStore::SshKeychainStore(QString filePath) : m_filePath(std::move(filePath)) {}

const QString &SshKeychainStore::filePath() const noexcept
{
    return m_filePath;
}

bool SshKeychainStore::lastLoadRecoveredFromBackup() const noexcept
{
    return m_lastLoadRecoveredFromBackup;
}

std::expected<SshKeychainCatalog, SshKeychainStoreError> SshKeychainStore::load() const
{
    m_lastLoadRecoveredFromBackup = false;
    auto loaded = ztermy::persistence::loadLastKnownGood(m_filePath, maximumFileSize, validatePayload);
    if (!loaded)
    {
        return std::unexpected(storeError(loaded.error()));
    }
    if (!loaded->has_value())
    {
        return SshKeychainCatalog{};
    }
    m_lastLoadRecoveredFromBackup = loaded->value().recoveredFromBackup;
    return parsePayload(loaded->value().bytes);
}

std::expected<void, SshKeychainStoreError> SshKeychainStore::save(const SshKeychainCatalog &catalog) const
{
    if (!validCatalog(catalog))
    {
        return std::unexpected(SshKeychainStoreError::InvalidFormat);
    }
    QJsonArray keys;
    for (const SshKeyRecord &record : catalog.keys)
    {
        keys.append(serializeKey(record));
    }
    QJsonArray identities;
    for (const SshIdentity &identity : catalog.identities)
    {
        identities.append(serializeIdentity(identity));
    }
    const QByteArray payload =
        QJsonDocument(QJsonObject{
                          {QStringLiteral("version"), currentSchemaVersion},
                          {QStringLiteral("legacyProfilesMigrated"), catalog.legacyProfilesMigrated},
                          {QStringLiteral("keys"), keys},
                          {QStringLiteral("identities"), identities},
                      })
            .toJson(QJsonDocument::Indented);
    const auto saved = ztermy::persistence::saveLastKnownGood(m_filePath, payload, maximumFileSize, validatePayload);
    return saved ? std::expected<void, SshKeychainStoreError>{} : std::unexpected(storeError(saved.error()));
}

} // namespace ztermy::ssh
