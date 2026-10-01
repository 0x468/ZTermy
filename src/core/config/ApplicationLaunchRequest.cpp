#include "core/config/ApplicationLaunchRequest.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace ztermy::config
{
namespace
{
[[nodiscard]] std::expected<QString, QString> existingDirectory(const QString &path)
{
    if (path.isEmpty() || path.contains(QChar::Null) || path.contains(u'\n') || path.contains(u'\r'))
        return std::unexpected(QStringLiteral("--open-directory requires an existing directory"));
    const QFileInfo info(path);
    if (!info.isDir())
        return std::unexpected(QStringLiteral("The requested directory does not exist or is not accessible"));
    return QDir::cleanPath(info.absoluteFilePath());
}
} // namespace

QByteArray ApplicationLaunchRequest::toMessage() const
{
    return QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                     {QStringLiteral("directory"), directory},
                                     {QStringLiteral("background"), background}})
               .toJson(QJsonDocument::Compact)
           + '\n';
}

std::expected<ApplicationLaunchRequest, QString> ApplicationLaunchRequest::fromMessage(const QByteArray &message)
{
    if (message == "activate\n")
        return ApplicationLaunchRequest{}; // Previous launches remain readable.
    if (message.size() > 65536)
        return std::unexpected(QStringLiteral("Launch request is too large"));
    const auto document = QJsonDocument::fromJson(message);
    const auto json = document.object();
    if (!document.isObject() || json.size() != 3 || json.value(QStringLiteral("version")) != QJsonValue(1)
        || !json.value(QStringLiteral("directory")).isString() || !json.value(QStringLiteral("background")).isBool())
        return std::unexpected(QStringLiteral("Invalid launch request"));
    ApplicationLaunchRequest result{.directory = json.value(QStringLiteral("directory")).toString(),
                                    .background = json.value(QStringLiteral("background")).toBool()};
    if (!result.directory.isEmpty())
    {
        if (!QDir::isAbsolutePath(result.directory) || result.background || result.directory.contains(QChar::Null)
            || result.directory.contains(u'\n') || result.directory.contains(u'\r'))
            return std::unexpected(QStringLiteral("Invalid directory launch request"));
        // Do not stat paths (in particular UNC paths) on the receiving GUI thread.
        result.directory = QDir::cleanPath(result.directory);
    }
    return result;
}

std::expected<ApplicationLaunchRequest, QString> ApplicationLaunchRequest::fromArguments(const QStringList &arguments)
{
    ApplicationLaunchRequest result;
    bool directorySpecified = false;
    for (qsizetype index = 1; index < arguments.size(); ++index)
    {
        const auto &argument = arguments[index];
        if (argument == QStringLiteral("--background"))
            result.background = true;
        else if (argument == QStringLiteral("--open-directory")
                 || argument.startsWith(QStringLiteral("--open-directory=")))
        {
            if (directorySpecified)
                return std::unexpected(QStringLiteral("--open-directory may only be specified once"));
            directorySpecified = true;
            QString path;
            if (argument == QStringLiteral("--open-directory"))
            {
                if (++index >= arguments.size())
                    return std::unexpected(QStringLiteral("--open-directory requires a path"));
                path = arguments[index];
            }
            else
                path = argument.sliced(QStringLiteral("--open-directory=").size());
            auto directory = existingDirectory(path);
            if (!directory)
                return std::unexpected(directory.error());
            result.directory = *directory;
        }
    }
    // An explicit directory always takes precedence over login's hidden launch.
    if (directorySpecified)
        result.background = false;
    return result;
}
} // namespace ztermy::config
