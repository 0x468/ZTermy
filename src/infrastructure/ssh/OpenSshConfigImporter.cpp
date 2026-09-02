#include "infrastructure/ssh/OpenSshConfigImporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>

#include <algorithm>

namespace
{

constexpr qint64 maximumConfigBytes = qint64{4} * 1024 * 1024;
constexpr int maximumIncludeDepth = 8;

[[nodiscard]] bool exactAlias(const QString &value)
{
    return !value.isEmpty() && !value.contains(QLatin1Char('*')) && !value.contains(QLatin1Char('?'))
           && !value.contains(QLatin1Char('!'));
}

[[nodiscard]] QString expandedPath(const QString &value, const QString &baseDirectory)
{
    if (value == QStringLiteral("~"))
        return QDir::homePath();
    if (value.startsWith(QStringLiteral("~/")) || value.startsWith(QStringLiteral("~\\")))
        return QDir::home().filePath(value.sliced(2));
    return QFileInfo(value).isAbsolute() ? QDir::cleanPath(value) : QDir(baseDirectory).filePath(value);
}

struct Parser final
{
    ztermy::ssh::OpenSshConfigImport result;
    QSet<QString> visited;

    [[nodiscard]] bool parseFile(const QString &path, const int depth)
    {
        const QString canonical = QFileInfo(path).canonicalFilePath();
        if (canonical.isEmpty() || depth > maximumIncludeDepth || visited.contains(canonical))
            return false;
        QFile file(canonical);
        if (!file.open(QIODevice::ReadOnly) || file.size() > maximumConfigBytes)
            return false;
        visited.insert(canonical);
        ++result.parsedFiles;
        const QString base = QFileInfo(canonical).dir().absolutePath();
        std::vector<std::size_t> active;
        while (!file.atEnd())
        {
            QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            const qsizetype comment = line.indexOf(QStringLiteral(" #"));
            if (comment >= 0)
                line.truncate(comment);
            const QStringList fields = QProcess::splitCommand(line);
            if (fields.size() < 2)
                continue;
            const QString key = fields.front().toLower();
            if (key == QStringLiteral("include") && active.empty())
            {
                for (qsizetype index = 1; index < fields.size(); ++index)
                {
                    const QString expanded = expandedPath(fields.at(index), base);
                    const QFileInfo includeInfo(expanded);
                    const QString directory = includeInfo.dir().absolutePath();
                    const QString pattern = includeInfo.fileName();
                    const QStringList matches = QDir(directory).entryList({pattern}, QDir::Files, QDir::Name);
                    for (const QString &match : matches)
                        static_cast<void>(parseFile(QDir(directory).filePath(match), depth + 1));
                }
                continue;
            }
            if (key == QStringLiteral("host"))
            {
                active.clear();
                for (qsizetype index = 1; index < fields.size(); ++index)
                {
                    if (!exactAlias(fields.at(index)))
                    {
                        ++result.skippedHosts;
                        continue;
                    }
                    result.hosts.push_back({.alias = fields.at(index), .hostName = fields.at(index)});
                    active.push_back(result.hosts.size() - 1);
                }
                continue;
            }
            const QString value = fields.sliced(1).join(QLatin1Char(' '));
            for (const std::size_t index : active)
            {
                auto &host = result.hosts[index];
                if (key == QStringLiteral("hostname") && host.hostName == host.alias)
                    host.hostName = value;
                else if (key == QStringLiteral("user") && host.user.isEmpty())
                    host.user = value;
                else if (key == QStringLiteral("port") && host.port == 22)
                {
                    bool ok = false;
                    const int port = value.toInt(&ok);
                    if (ok && port > 0 && port <= 65535)
                        host.port = static_cast<std::uint16_t>(port);
                }
                else if (key == QStringLiteral("identityfile") && host.identityFile.isEmpty())
                    host.identityFile = expandedPath(value, base);
                else if (key == QStringLiteral("proxyjump") && host.proxyJump.isEmpty())
                    host.proxyJump = value.section(QLatin1Char(','), 0, 0).trimmed();
            }
        }
        return true;
    }
};

} // namespace

namespace ztermy::ssh
{

std::expected<OpenSshConfigImport, OpenSshConfigImportError> loadOpenSshConfig(const QString &filePath)
{
    const QFileInfo info(filePath);
    if (filePath.trimmed().isEmpty() || !info.exists() || !info.isFile())
        return std::unexpected(OpenSshConfigImportError::invalidPath);
    if (info.size() > maximumConfigBytes)
        return std::unexpected(OpenSshConfigImportError::tooLarge);
    Parser parser;
    if (!parser.parseFile(info.absoluteFilePath(), 0))
        return std::unexpected(OpenSshConfigImportError::io);
    std::ranges::stable_sort(parser.result.hosts, {}, &OpenSshHostConfig::alias);
    const auto duplicateRange =
        std::ranges::unique(parser.result.hosts, [](const OpenSshHostConfig &left, const OpenSshHostConfig &right) {
            return left.alias.compare(right.alias, Qt::CaseInsensitive) == 0;
        });
    parser.result.hosts.erase(duplicateRange.begin(), duplicateRange.end());
    return parser.result;
}

} // namespace ztermy::ssh
