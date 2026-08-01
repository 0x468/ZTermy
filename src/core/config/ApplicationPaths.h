#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>
#include <expected>

namespace ztermy::config
{

enum class StorageMode : std::uint8_t
{
    installed,
    portable,
    custom,
};

struct ApplicationPaths final
{
    StorageMode mode = StorageMode::installed;
    QString dataDirectory;
    QString localDataDirectory;
    QString profilesFile;
    QString knownHostsFile;
    QString settingsFile;
    QString credentialsFile;
    QString logsDirectory;
    QString crashDirectory;
};

[[nodiscard]] std::expected<ApplicationPaths, QString>
resolveApplicationPaths(const QStringList &arguments, const QString &executableDirectory,
                        const QString &workingDirectory, const QString &installedDataDirectory,
                        const QString &installedLocalDataDirectory, bool portableMarkerPresent);

[[nodiscard]] std::expected<void, QString> prepareApplicationPaths(const ApplicationPaths &paths);
[[nodiscard]] QString storageModeName(StorageMode mode);

} // namespace ztermy::config
