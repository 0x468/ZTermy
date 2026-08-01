#include "core/config/ApplicationPaths.h"

#include <QDir>
#include <QFileInfo>

#include <optional>
#include <utility>

namespace
{

[[nodiscard]] QString absoluteDirectory(const QString &path, const QString &workingDirectory)
{
    const QString absolutePath = QDir::isAbsolutePath(path) ? path : QDir(workingDirectory).absoluteFilePath(path);
    return QDir::cleanPath(QFileInfo(absolutePath).absoluteFilePath());
}

[[nodiscard]] std::expected<std::optional<QString>, QString> customDataDirectory(const QStringList &arguments)
{
    std::optional<QString> result;
    for (qsizetype index = 1; index < arguments.size(); ++index)
    {
        const QString &argument = arguments[index];
        QString value;
        if (argument == QStringLiteral("--data-dir"))
        {
            ++index;
            if (index >= arguments.size() || arguments[index].trimmed().isEmpty())
            {
                return std::unexpected(QStringLiteral("--data-dir requires a non-empty path"));
            }
            value = arguments[index];
        }
        else if (argument.startsWith(QStringLiteral("--data-dir=")))
        {
            value = argument.sliced(QStringLiteral("--data-dir=").size());
            if (value.trimmed().isEmpty())
            {
                return std::unexpected(QStringLiteral("--data-dir requires a non-empty path"));
            }
        }
        else
        {
            continue;
        }

        value = value.trimmed();
        if (result && *result != value)
        {
            return std::unexpected(QStringLiteral("--data-dir may only specify one path"));
        }
        result = std::move(value);
    }
    return result;
}

} // namespace

namespace ztermy::config
{

std::expected<ApplicationPaths, QString>
resolveApplicationPaths(const QStringList &arguments, const QString &executableDirectory,
                        const QString &workingDirectory, const QString &installedDataDirectory,
                        const QString &installedLocalDataDirectory, const bool portableMarkerPresent)
{
    if (executableDirectory.trimmed().isEmpty() || workingDirectory.trimmed().isEmpty()
        || installedDataDirectory.trimmed().isEmpty() || installedLocalDataDirectory.trimmed().isEmpty())
    {
        return std::unexpected(QStringLiteral("Application data paths must not be empty"));
    }

    const auto customDirectory = customDataDirectory(arguments);
    if (!customDirectory)
    {
        return std::unexpected(customDirectory.error());
    }

    ApplicationPaths paths;
    if (*customDirectory)
    {
        paths.mode = StorageMode::custom;
        paths.dataDirectory = absoluteDirectory(**customDirectory, workingDirectory);
        paths.localDataDirectory = paths.dataDirectory;
    }
    else if (arguments.contains(QStringLiteral("--portable")) || portableMarkerPresent)
    {
        paths.mode = StorageMode::portable;
        paths.dataDirectory = QDir(executableDirectory).absoluteFilePath(QStringLiteral("data"));
        paths.localDataDirectory = paths.dataDirectory;
    }
    else
    {
        paths.mode = StorageMode::installed;
        paths.dataDirectory = absoluteDirectory(installedDataDirectory, workingDirectory);
        paths.localDataDirectory = absoluteDirectory(installedLocalDataDirectory, workingDirectory);
    }

    paths.dataDirectory = QDir::cleanPath(paths.dataDirectory);
    paths.localDataDirectory = QDir::cleanPath(paths.localDataDirectory);
    paths.profilesFile = QDir(paths.dataDirectory).filePath(QStringLiteral("profiles.json"));
    paths.knownHostsFile = QDir(paths.dataDirectory).filePath(QStringLiteral("known_hosts.json"));
    paths.settingsFile = QDir(paths.dataDirectory).filePath(QStringLiteral("settings.json"));
    paths.credentialsFile = QDir(paths.dataDirectory).filePath(QStringLiteral("credentials.zvlt"));
    paths.logsDirectory = QDir(paths.localDataDirectory).filePath(QStringLiteral("logs"));
    paths.crashDirectory = QDir(paths.localDataDirectory).filePath(QStringLiteral("crashes"));
    return paths;
}

std::expected<void, QString> prepareApplicationPaths(const ApplicationPaths &paths)
{
    const QStringList directories = {
        paths.dataDirectory,
        paths.localDataDirectory,
        paths.logsDirectory,
        paths.crashDirectory,
    };
    for (const QString &directory : directories)
    {
        if (directory.isEmpty() || !QDir().mkpath(directory))
        {
            return std::unexpected(QStringLiteral("Unable to create application data directory: %1").arg(directory));
        }
    }
    return {};
}

QString storageModeName(const StorageMode mode)
{
    switch (mode)
    {
        case StorageMode::portable:
            return QStringLiteral("portable");
        case StorageMode::custom:
            return QStringLiteral("custom");
        case StorageMode::installed:
        default:
            return QStringLiteral("installed");
    }
}

} // namespace ztermy::config
