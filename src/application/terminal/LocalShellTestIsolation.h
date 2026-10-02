#pragma once

#include <QFileInfo>
#include <QStringList>

namespace ztermy::terminal
{

// Opt-in test policy only. Ordinary sessions retain the user's startup files.
[[nodiscard]] inline bool isolatedShellTestsEnabled()
{
    return qEnvironmentVariableIntValue("ZTERMY_TEST_ISOLATED_SHELLS") == 1;
}

[[nodiscard]] inline QStringList isolatedShellArguments(const QString &executable, QStringList arguments,
                                                        const bool isolated)
{
    if (!isolated)
        return arguments;
    const QString name = QFileInfo(executable).fileName().toLower();
    const auto prepend = [&arguments](const QString &flag) {
        if (!arguments.contains(flag, Qt::CaseInsensitive))
            arguments.prepend(flag);
    };
    if (name == QStringLiteral("cmd.exe"))
        prepend(QStringLiteral("/D"));
    else if (name == QStringLiteral("pwsh.exe") || name == QStringLiteral("powershell.exe"))
        prepend(QStringLiteral("-NoProfile"));
    else if (name == QStringLiteral("nu.exe"))
    {
        prepend(QStringLiteral("--no-history"));
        prepend(QStringLiteral("--no-config-file"));
    }
    else if (name == QStringLiteral("bash.exe"))
    {
        prepend(QStringLiteral("--norc"));
        prepend(QStringLiteral("--noprofile"));
    }
    return arguments;
}

} // namespace ztermy::terminal
