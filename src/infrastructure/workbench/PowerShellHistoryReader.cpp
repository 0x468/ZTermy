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
    return parsePowerShellHistory(std::string_view(contents.constData(), static_cast<std::size_t>(contents.size())),
                                  maximumEntries);
}

} // namespace ztermy::workbench
