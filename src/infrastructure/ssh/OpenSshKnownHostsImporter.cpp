#include "infrastructure/ssh/OpenSshKnownHostsImporter.h"

#include <QByteArray>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace
{

constexpr qint64 maximumFileSize = 4LL * 1024 * 1024;
constexpr std::size_t maximumImportedEntries = 4096;
constexpr qsizetype maximumEncodedKeySize = 16LL * 1024;

[[nodiscard]] std::optional<ztermy::ssh::SshEndpoint> parseEndpoint(const QString &rawToken)
{
    const QString token = rawToken.trimmed();
    if (token.isEmpty() || token.startsWith(QLatin1Char('|')) || token.startsWith(QLatin1Char('!'))
        || token.contains(QLatin1Char('*')) || token.contains(QLatin1Char('?')))
    {
        return std::nullopt;
    }

    QString host = token;
    int port = 22;
    if (token.startsWith(QLatin1Char('[')))
    {
        const qsizetype closingBracket = token.indexOf(QStringLiteral("]:"));
        if (closingBracket <= 1)
        {
            return std::nullopt;
        }
        bool validPort = false;
        const int parsedPort = token.mid(closingBracket + 2).toInt(&validPort);
        if (!validPort || parsedPort <= 0 || parsedPort > 65535)
        {
            return std::nullopt;
        }
        host = token.mid(1, closingBracket - 1);
        port = parsedPort;
    }
    if (host.isEmpty() || host.size() > 1024 || host.contains(QChar::Null))
    {
        return std::nullopt;
    }
    const QByteArray hostUtf8 = host.toUtf8();
    return ztermy::ssh::SshEndpoint{
        .host = std::string(hostUtf8.constData(), static_cast<std::size_t>(hostUtf8.size())),
        .port = static_cast<std::uint16_t>(port),
    };
}

[[nodiscard]] bool duplicate(const std::vector<ztermy::ssh::KnownHostEntry> &entries,
                             const ztermy::ssh::KnownHostEntry &candidate)
{
    return std::ranges::any_of(entries, [&candidate](const ztermy::ssh::KnownHostEntry &entry) {
        return entry.endpoint == candidate.endpoint && entry.algorithm == candidate.algorithm;
    });
}

} // namespace

namespace ztermy::ssh
{

OpenSshKnownHostsImport parseOpenSshKnownHosts(const QByteArray &contents)
{
    OpenSshKnownHostsImport result;
    const QList<QByteArray> lines = contents.split('\n');
    for (const QByteArray &rawLine : lines)
    {
        const QByteArray trimmed = rawLine.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#'))
        {
            continue;
        }
        ++result.parsedLines;

        const QStringList fields =
            QString::fromUtf8(trimmed).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        qsizetype offset = 0;
        if (!fields.isEmpty() && fields.front().startsWith(QLatin1Char('@')))
        {
            ++offset;
        }
        if (fields.size() - offset < 3 || offset != 0)
        {
            ++result.unsupportedEntries;
            ++result.skippedLines;
            continue;
        }

        const QByteArray algorithmUtf8 = fields[offset + 1].toUtf8();
        const auto algorithm = parseHostKeyAlgorithm(
            std::string_view(algorithmUtf8.constData(), static_cast<std::size_t>(algorithmUtf8.size())));
        const QByteArray key =
            QByteArray::fromBase64(fields[offset + 2].toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!algorithm || key.isEmpty() || key.size() > maximumEncodedKeySize)
        {
            ++result.unsupportedEntries;
            ++result.skippedLines;
            continue;
        }

        bool handledLine = false;
        const QStringList hostTokens = fields[offset].split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &hostToken : hostTokens)
        {
            const auto endpoint = parseEndpoint(hostToken);
            if (!endpoint || result.entries.size() >= maximumImportedEntries)
            {
                ++result.unsupportedEntries;
                continue;
            }
            KnownHostEntry entry{
                .endpoint = *endpoint,
                .algorithm = *algorithm,
                .encodedKey =
                    std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t *>(key.constData()),
                                              reinterpret_cast<const std::uint8_t *>(key.constData()) + key.size()),
            };
            if (duplicate(result.entries, entry))
            {
                ++result.duplicateEntries;
                handledLine = true;
                continue;
            }
            result.entries.push_back(std::move(entry));
            handledLine = true;
        }
        if (!handledLine)
        {
            ++result.skippedLines;
        }
    }
    return result;
}

std::expected<OpenSshKnownHostsImport, OpenSshKnownHostsImportError> loadOpenSshKnownHosts(const QString &filePath)
{
    if (filePath.trimmed().isEmpty())
    {
        return std::unexpected(OpenSshKnownHostsImportError::InvalidPath);
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::unexpected(OpenSshKnownHostsImportError::IoError);
    }
    if (file.size() < 0 || file.size() > maximumFileSize)
    {
        return std::unexpected(OpenSshKnownHostsImportError::TooLarge);
    }
    return parseOpenSshKnownHosts(file.readAll());
}

} // namespace ztermy::ssh
