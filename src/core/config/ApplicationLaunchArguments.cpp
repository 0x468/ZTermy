#include "core/config/ApplicationLaunchRequest.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QUrl>

namespace ztermy::config
{
QString ApplicationLaunchRequest::helpText()
{
    return QStringLiteral(
        "Ztermy launch options\n\n"
        "  --open-directory PATH [--local-shell ID] [--window main|detached]\n"
        "  --ssh [ssh://]USER@HOST[:PORT][/REMOTE-PATH]\n"
        "  --ssh HOST --user USER [--port PORT]\n"
        "  --profile SAVED-HOST-ID\n"
        "\nSSH options:\n"
        "  --auth password|private-key|agent (default: password, or private-key with --identity)\n"
        "  --identity PRIVATE-KEY [--certificate OPENSSH-CERTIFICATE]\n"
        "  --password-file LOCAL-FILE (password or key passphrase; UTF-8, one line)\n"
        "  --remote-directory PATH (POSIX shell; literal path, not a command)\n"
        "  --window main|detached (default: main)\n"
        "\nWithout a credential file, passwords/passphrases are prompted for.\n"
        "Saved hosts reuse their identity and credential configuration.\n"
        "Passwords in arguments/URLs and PuTTY -pw are rejected.\n"
        "Temporary SSH launches do not save host profiles.\n"
        "Built-in Shell IDs: automatic, powerShellCore, windowsPowerShell, commandPrompt, gitBash, nushell, wsl.\n"
        "\nSee docs/COMMAND_LINE.md for WinSCP integration and examples.\n");
}

std::expected<ApplicationLaunchRequest, QString> ApplicationLaunchRequest::fromArguments(const QStringList &arguments)
{
    ApplicationLaunchRequest result;
    QMap<QString, QString> options;
    const QSet<QString> recognized{
        QStringLiteral("--open-directory"), QStringLiteral("--local-shell"),   QStringLiteral("--window"),
        QStringLiteral("--profile"),        QStringLiteral("--ssh"),           QStringLiteral("--user"),
        QStringLiteral("--port"),           QStringLiteral("--auth"),          QStringLiteral("--identity"),
        QStringLiteral("--certificate"),    QStringLiteral("--password-file"), QStringLiteral("--remote-directory")};
    for (qsizetype index = 1; index < arguments.size(); ++index)
    {
        const auto &argument = arguments[index];
        if (argument == QStringLiteral("--background"))
        {
            result.background = true;
            continue;
        }
        if (argument == QStringLiteral("-pw")
            || (argument.startsWith(QStringLiteral("--password"))
                && !argument.startsWith(QStringLiteral("--password-file"))))
            return std::unexpected(
                QStringLiteral("Use --password-file or interactive authentication, not a password argument"));
        if (argument.startsWith(QStringLiteral("ssh://")))
        {
            if (options.contains(QStringLiteral("--ssh")))
                return std::unexpected(QStringLiteral("Specify only one SSH target"));
            options.insert(QStringLiteral("--ssh"), argument);
            continue;
        }
        const auto equals = argument.indexOf(u'=');
        const auto name = equals < 0 ? argument : argument.first(equals);
        if (!recognized.contains(name))
            continue; // Runtime/test/storage switches remain owned by their existing entry points.
        if (options.contains(name))
            return std::unexpected(QStringLiteral("Duplicate launch option"));
        QString value;
        if (equals >= 0)
            value = argument.sliced(equals + 1);
        else
        {
            ++index;
            if (index < arguments.size() && !arguments[index].startsWith(u'-'))
                value = arguments[index];
        }
        if (value.isEmpty())
            return std::unexpected(QStringLiteral("Launch option requires a value"));
        options.insert(name, value);
    }
    result.directory = options.value(QStringLiteral("--open-directory"));
    if (!result.directory.isEmpty())
    {
        const QFileInfo directory(result.directory);
        if (!directory.isDir())
            return std::unexpected(QStringLiteral("The requested directory does not exist or is not accessible"));
        result.directory = QDir::cleanPath(directory.absoluteFilePath());
    }
    result.shellId = options.value(QStringLiteral("--local-shell"));
    result.windowMode = options.value(QStringLiteral("--window"), QStringLiteral("main"));
    result.profileId = options.value(QStringLiteral("--profile"));
    result.username = options.value(QStringLiteral("--user"));
    result.remoteDirectory = options.value(QStringLiteral("--remote-directory"));
    result.authentication = options.value(QStringLiteral("--auth"));
    result.identityFile = options.value(QStringLiteral("--identity"));
    result.certificateFile = options.value(QStringLiteral("--certificate"));
    result.passwordFile = options.value(QStringLiteral("--password-file"));
    const auto target = options.value(QStringLiteral("--ssh"));
    if (!target.isEmpty())
    {
        if (std::ranges::any_of(target, [](QChar c) {
                return c.isNull() || c.category() == QChar::Other_Control;
            }))
            return std::unexpected(QStringLiteral("Invalid SSH address"));
        const auto url = QUrl(target.startsWith(QStringLiteral("ssh://")) ? target : QStringLiteral("ssh://") + target,
                              QUrl::StrictMode);
        if (!url.isValid() || url.scheme() != QStringLiteral("ssh") || url.host().isEmpty() || !url.password().isEmpty()
            || url.userInfo().contains(u':') || url.hasQuery() || url.hasFragment())
            return std::unexpected(QStringLiteral("Invalid SSH address; URL passwords are not accepted"));
        result.host = url.host();
        if (!url.userName().isEmpty())
        {
            if (!result.username.isEmpty())
                return std::unexpected(QStringLiteral("Specify the SSH user only once"));
            result.username = url.userName();
        }
        result.port = url.port(22);
        if (!url.path().isEmpty() && url.path() != QStringLiteral("/"))
        {
            if (!result.remoteDirectory.isEmpty())
                return std::unexpected(QStringLiteral("Specify the remote directory only once"));
            result.remoteDirectory = url.path();
        }
        if (result.authentication.isEmpty())
            result.authentication =
                result.identityFile.isEmpty() ? QStringLiteral("password") : QStringLiteral("private-key");
    }
    if (options.contains(QStringLiteral("--port")))
    {
        if (target.isEmpty())
            return std::unexpected(QStringLiteral("--port requires an SSH target"));
        bool validPort = false;
        result.port = options.value(QStringLiteral("--port")).toInt(&validPort);
        if (!validPort)
            return std::unexpected(QStringLiteral("Invalid SSH port"));
    }
    for (auto *path : {&result.identityFile, &result.certificateFile, &result.passwordFile})
        if (!path->isEmpty())
            *path = QDir::cleanPath(QFileInfo(*path).absoluteFilePath());
    if (result.hasTarget())
        result.background = false;
    if (!result.valid())
        return std::unexpected(QStringLiteral("Invalid or conflicting launch options"));
    return result;
}
} // namespace ztermy::config
