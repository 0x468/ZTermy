#include "infrastructure/workbench/PowerShellHistoryReader.h"

#include <QDir>
#include <QFile>

#include <algorithm>

namespace ztermy::workbench
{

QString defaultPowerShellHistoryPath()
{
    const QString roamingData = qEnvironmentVariable("APPDATA").trimmed();
    if (roamingData.isEmpty())
    {
        return {};
    }
    return QDir(roamingData)
        .filePath(QStringLiteral("Microsoft/Windows/PowerShell/PSReadLine/ConsoleHost_history.txt"));
}

std::expected<std::vector<ShellHistoryEntry>, PowerShellHistoryReadError>
readPowerShellHistory(const QString &path, const std::size_t maximumEntries, const qint64 maximumSourceBytes)
{
    return readShellHistoryFile(path, ShellKind::powershell, maximumEntries, maximumSourceBytes);
}

QString defaultLocalShellHistoryPath(const QString &shellId)
{
    if (shellId == QStringLiteral("powerShellCore") || shellId == QStringLiteral("windowsPowerShell"))
        return defaultPowerShellHistoryPath();
    if (shellId == QStringLiteral("gitBash"))
    {
        const QString home = qEnvironmentVariable("HOME");
        return QDir(home.isEmpty() ? QDir::homePath() : home).filePath(QStringLiteral(".bash_history"));
    }
    // Unknown/custom Shells must never fall back to another Shell's history.
    return {};
}

std::expected<std::vector<ShellHistoryEntry>, PowerShellHistoryReadError>
readShellHistoryFile(const QString &path, const ShellKind shell, const std::size_t maximumEntries,
                     const qint64 maximumSourceBytes)
{
    if (path.trimmed().isEmpty() || maximumSourceBytes <= 0)
    {
        return std::unexpected(PowerShellHistoryReadError::invalidPath);
    }

    QFile file(path);
    if (!file.exists())
    {
        return std::vector<ShellHistoryEntry>{};
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() < 0)
    {
        return std::unexpected(PowerShellHistoryReadError::ioError);
    }

    const qint64 offset = std::max<qint64>(0, file.size() - maximumSourceBytes);
    if (offset > 0 && !file.seek(offset))
    {
        return std::unexpected(PowerShellHistoryReadError::ioError);
    }
    QByteArray contents = file.read(maximumSourceBytes);
    if (file.error() != QFileDevice::NoError)
    {
        return std::unexpected(PowerShellHistoryReadError::ioError);
    }
    if (offset > 0)
    {
        const qsizetype firstCompleteLine = contents.indexOf('\n');
        contents = firstCompleteLine < 0 ? QByteArray{} : contents.sliced(firstCompleteLine + 1);
    }
    const std::string_view text(contents.constData(), static_cast<std::size_t>(contents.size()));
    switch (shell)
    {
        case ShellKind::powershell:
            return parsePowerShellHistory(text, maximumEntries);
        case ShellKind::bash:
            return parseBashHistory(text, maximumEntries);
        case ShellKind::zsh:
            return parseZshHistory(text, maximumEntries);
        case ShellKind::fish:
            return parseFishHistory(text, maximumEntries);
        default:
            return std::unexpected(PowerShellHistoryReadError::invalidPath);
    }
}

} // namespace ztermy::workbench
