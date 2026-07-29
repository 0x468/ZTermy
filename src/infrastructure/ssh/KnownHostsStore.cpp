#include "infrastructure/ssh/KnownHostsStore.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace
{

constexpr qint64 MaximumFileSize = 1024LL * 1024;
constexpr qsizetype MaximumEntryCount = 4096;
constexpr qsizetype MaximumEncodedKeySize = 16LL * 1024;
constexpr qsizetype MaximumHostLength = 1024;
constexpr int CurrentSchemaVersion = 1;

[[nodiscard]] bool duplicateEndpointAlgorithm(const std::span<const ztermy::ssh::KnownHostEntry> entries,
                                              const ztermy::ssh::KnownHostEntry &candidate) noexcept
{
    return std::ranges::any_of(entries, [&candidate](const ztermy::ssh::KnownHostEntry &entry) {
        return entry.endpoint == candidate.endpoint && entry.algorithm == candidate.algorithm;
    });
}

[[nodiscard]] bool validEntry(const ztermy::ssh::KnownHostEntry &entry) noexcept
{
    return !entry.endpoint.host.empty() && entry.endpoint.host.size() <= static_cast<std::size_t>(MaximumHostLength)
           && entry.endpoint.host.find('\0') == std::string::npos && entry.endpoint.port != 0
           && entry.algorithm != ztermy::ssh::HostKeyAlgorithm::Unknown && !entry.encodedKey.empty()
           && entry.encodedKey.size() <= static_cast<std::size_t>(MaximumEncodedKeySize);
}

[[nodiscard]] std::optional<ztermy::ssh::KnownHostEntry> parseEntry(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }

    const QJsonObject object = value.toObject();
    const QJsonValue hostValue = object.value(QStringLiteral("host"));
    const QJsonValue portValue = object.value(QStringLiteral("port"));
    const QJsonValue algorithmValue = object.value(QStringLiteral("algorithm"));
    const QJsonValue keyValue = object.value(QStringLiteral("key"));
    if (!hostValue.isString() || !portValue.isDouble() || !algorithmValue.isString() || !keyValue.isString())
    {
        return std::nullopt;
    }

    const QString host = hostValue.toString();
    const double portNumber = portValue.toDouble();
    const QByteArray algorithmUtf8 = algorithmValue.toString().toUtf8();
    const auto algorithm = ztermy::ssh::parseHostKeyAlgorithm(
        std::string_view(algorithmUtf8.constData(), static_cast<std::size_t>(algorithmUtf8.size())));
    const QByteArray encodedKeyText = keyValue.toString().toLatin1();
    const QByteArray encodedKey = QByteArray::fromBase64(encodedKeyText, QByteArray::AbortOnBase64DecodingErrors);
    if (host.isEmpty() || host.size() > MaximumHostLength || host.contains(QChar::Null) || !std::isfinite(portNumber)
        || std::floor(portNumber) != portNumber || portNumber <= 0 || portNumber > 65535 || !algorithm
        || encodedKey.isEmpty() || encodedKey.size() > MaximumEncodedKeySize)
    {
        return std::nullopt;
    }

    const QByteArray hostUtf8 = host.toUtf8();
    ztermy::ssh::KnownHostEntry entry{
        .endpoint =
            {
                .host = std::string(hostUtf8.constData(), static_cast<std::size_t>(hostUtf8.size())),
                .port = static_cast<std::uint16_t>(portNumber),
            },
        .algorithm = *algorithm,
        .encodedKey = std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t *>(encodedKey.constData()),
                                                reinterpret_cast<const std::uint8_t *>(encodedKey.constData())
                                                    + encodedKey.size()),
    };
    if (!validEntry(entry))
    {
        return std::nullopt;
    }
    return entry;
}

[[nodiscard]] QJsonObject serializeEntry(const ztermy::ssh::KnownHostEntry &entry)
{
    const QByteArray encodedKey(reinterpret_cast<const char *>(entry.encodedKey.data()),
                                static_cast<qsizetype>(entry.encodedKey.size()));
    return {
        {QStringLiteral("host"), QString::fromUtf8(entry.endpoint.host)},
        {QStringLiteral("port"), static_cast<qint64>(entry.endpoint.port)},
        {QStringLiteral("algorithm"), QString::fromLatin1(ztermy::ssh::hostKeyAlgorithmToken(entry.algorithm))},
        {QStringLiteral("key"), QString::fromLatin1(encodedKey.toBase64())},
    };
}

} // namespace

namespace ztermy::ssh
{

KnownHostsStore::KnownHostsStore(QString filePath) : m_filePath(std::move(filePath)) {}

const QString &KnownHostsStore::filePath() const noexcept
{
    return m_filePath;
}

std::expected<std::vector<KnownHostEntry>, KnownHostsStoreError> KnownHostsStore::load() const
{
    if (m_filePath.isEmpty())
    {
        return std::unexpected(KnownHostsStoreError::InvalidPath);
    }

    QFile file(m_filePath);
    if (!file.exists())
    {
        return std::vector<KnownHostEntry>{};
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() < 0 || file.size() > MaximumFileSize)
    {
        return std::unexpected(file.size() > MaximumFileSize ? KnownHostsStoreError::InvalidFormat
                                                             : KnownHostsStoreError::IoError);
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(KnownHostsStoreError::InvalidFormat);
    }

    const QJsonObject root = document.object();
    const QJsonValue versionValue = root.value(QStringLiteral("version"));
    const QJsonValue hostsValue = root.value(QStringLiteral("hosts"));
    if (!versionValue.isDouble() || versionValue.toInteger() != CurrentSchemaVersion)
    {
        return std::unexpected(versionValue.isDouble() ? KnownHostsStoreError::UnsupportedVersion
                                                       : KnownHostsStoreError::InvalidFormat);
    }
    if (!hostsValue.isArray())
    {
        return std::unexpected(KnownHostsStoreError::InvalidFormat);
    }

    const QJsonArray hosts = hostsValue.toArray();
    if (hosts.size() > MaximumEntryCount)
    {
        return std::unexpected(KnownHostsStoreError::InvalidFormat);
    }

    std::vector<KnownHostEntry> entries;
    entries.reserve(static_cast<std::size_t>(hosts.size()));
    for (const auto &value : hosts)
    {
        auto entry = parseEntry(value);
        if (!entry || duplicateEndpointAlgorithm(entries, *entry))
        {
            return std::unexpected(KnownHostsStoreError::InvalidFormat);
        }
        entries.push_back(std::move(*entry));
    }
    return entries;
}

std::expected<void, KnownHostsStoreError> KnownHostsStore::save(const std::span<const KnownHostEntry> entries) const
{
    if (m_filePath.isEmpty())
    {
        return std::unexpected(KnownHostsStoreError::InvalidPath);
    }
    if (entries.size() > static_cast<std::size_t>(MaximumEntryCount))
    {
        return std::unexpected(KnownHostsStoreError::InvalidFormat);
    }

    QJsonArray hosts;
    std::vector<KnownHostEntry> validated;
    validated.reserve(entries.size());
    for (const KnownHostEntry &entry : entries)
    {
        if (!validEntry(entry) || duplicateEndpointAlgorithm(validated, entry))
        {
            return std::unexpected(KnownHostsStoreError::InvalidFormat);
        }
        hosts.append(serializeEntry(entry));
        validated.push_back(entry);
    }

    const QFileInfo fileInfo(m_filePath);
    if (!fileInfo.absoluteDir().mkpath(QStringLiteral(".")))
    {
        return std::unexpected(KnownHostsStoreError::IoError);
    }

    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        return std::unexpected(KnownHostsStoreError::IoError);
    }

    const QJsonDocument document(QJsonObject{
        {QStringLiteral("version"), CurrentSchemaVersion},
        {QStringLiteral("hosts"), hosts},
    });
    const QByteArray data = document.toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit())
    {
        file.cancelWriting();
        return std::unexpected(KnownHostsStoreError::IoError);
    }
    return {};
}

} // namespace ztermy::ssh
