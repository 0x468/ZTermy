#include "application/terminal/WindowsLocalShellCatalog.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <Windows.h>

#include <array>

namespace
{

[[nodiscard]] QString firstExecutable(const QStringList &candidates)
{
    for (const QString &candidate : candidates)
    {
        if (!candidate.isEmpty() && QFileInfo(candidate).isExecutable())
        {
            return QDir::toNativeSeparators(QFileInfo(candidate).absoluteFilePath());
        }
    }
    return {};
}

[[nodiscard]] QString appPath(const QString &executable)
{
    const std::array roots{
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\"),
    };
    for (const QString &root : roots)
    {
        QSettings settings(root + executable, QSettings::NativeFormat);
        const QString path = settings.value(QStringLiteral(".")).toString();
        if (QFileInfo(path).isExecutable())
        {
            return path;
        }
    }
    return {};
}

[[nodiscard]] QString systemExecutable(const QString &name)
{
    std::array<wchar_t, MAX_PATH + 1> buffer{};
    const UINT count = GetSystemDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    if (count == 0 || count >= buffer.size())
    {
        return {};
    }
    return QDir(QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(count))).filePath(name);
}

[[nodiscard]] QString gitInstallLocation()
{
    const std::array keys{
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Git_is1"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Git_is1"),
        QStringLiteral(
            "HKEY_LOCAL_MACHINE\\Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Git_is1"),
    };
    for (const QString &key : keys)
    {
        QSettings settings(key, QSettings::NativeFormat);
        const QString location = settings.value(QStringLiteral("InstallLocation")).toString();
        if (!location.isEmpty())
        {
            return location;
        }
    }
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git.exe"));
    if (!git.isEmpty())
    {
        QDir directory(QFileInfo(git).absolutePath());
        if (directory.dirName().compare(QStringLiteral("cmd"), Qt::CaseInsensitive) == 0)
        {
            directory.cdUp();
            return directory.absolutePath();
        }
    }
    return {};
}

} // namespace

namespace ztermy::terminal
{

QList<LocalShellProfile> WindowsLocalShellCatalog::detect()
{
    const QString programFiles = qEnvironmentVariable("ProgramFiles");
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    const QString pwsh = firstExecutable({QStandardPaths::findExecutable(QStringLiteral("pwsh.exe")),
                                          appPath(QStringLiteral("pwsh.exe")),
                                          QDir(programFiles).filePath(QStringLiteral("PowerShell/7/pwsh.exe"))});
    const QString windowsPowerShell =
        firstExecutable({systemExecutable(QStringLiteral("WindowsPowerShell/v1.0/powershell.exe"))});
    const QString commandPrompt = firstExecutable({systemExecutable(QStringLiteral("cmd.exe"))});
    const QString gitRoot = gitInstallLocation();
    const QString gitBash = firstExecutable({QDir(gitRoot).filePath(QStringLiteral("bin/bash.exe")),
                                             QDir(programFiles).filePath(QStringLiteral("Git/bin/bash.exe")),
                                             QDir(localAppData).filePath(QStringLiteral("Programs/Git/bin/bash.exe"))});

    return {
        {.id = QStringLiteral("powerShellCore"),
         .name = QStringLiteral("PowerShell 7"),
         .executable = pwsh,
         .arguments = {QStringLiteral("-NoLogo")},
         .powerShellIntegration = true,
         .available = !pwsh.isEmpty()},
        {.id = QStringLiteral("windowsPowerShell"),
         .name = QStringLiteral("Windows PowerShell"),
         .executable = windowsPowerShell,
         .arguments = {QStringLiteral("-NoLogo")},
         .powerShellIntegration = true,
         .available = !windowsPowerShell.isEmpty()},
        {.id = QStringLiteral("commandPrompt"),
         .name = QStringLiteral("Command Prompt"),
         .executable = commandPrompt,
         .arguments = {QStringLiteral("/Q")},
         .available = !commandPrompt.isEmpty()},
        {.id = QStringLiteral("gitBash"),
         .name = QStringLiteral("Git Bash"),
         .executable = gitBash,
         .arguments = {QStringLiteral("--login"), QStringLiteral("-i")},
         .available = !gitBash.isEmpty()},
    };
}

std::optional<LocalShellProfile> WindowsLocalShellCatalog::resolve(const QList<LocalShellProfile> &profiles,
                                                                   const QString &preference)
{
    if (preference != QStringLiteral("automatic"))
    {
        for (const LocalShellProfile &profile : profiles)
        {
            if (profile.id == preference && profile.available)
            {
                return profile;
            }
        }
    }
    for (const QString &fallback :
         {QStringLiteral("powerShellCore"), QStringLiteral("windowsPowerShell"), QStringLiteral("commandPrompt")})
    {
        for (const LocalShellProfile &profile : profiles)
        {
            if (profile.id == fallback && profile.available)
            {
                return profile;
            }
        }
    }
    return std::nullopt;
}

LocalTerminalLaunchSpec WindowsLocalShellCatalog::launchSpec(const LocalShellProfile &profile,
                                                             const QString &workingDirectory)
{
    return {.id = profile.id,
            .displayName = profile.name,
            .executable = profile.executable,
            .arguments = profile.arguments,
            .workingDirectory = workingDirectory,
            .powerShellIntegration = profile.powerShellIntegration};
}

} // namespace ztermy::terminal
