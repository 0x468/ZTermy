#include "core/config/ApplicationLaunchRequest.h"
#include "core/config/WindowsIntegrationSettings.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace ztermy::config
{
namespace
{
bool safeText(const QString &value, const qsizetype limit)
{
    return value.size() <= limit && std::ranges::none_of(value, [](QChar c) {
               return c.isNull() || c.category() == QChar::Other_Control;
           });
}
bool safeFile(const QString &value)
{
    return value.isEmpty() || (safeText(value, 32768) && QDir::isAbsolutePath(value));
}
} // namespace

bool ApplicationLaunchRequest::hasTarget() const noexcept
{
    return !directory.isEmpty() || !host.isEmpty() || !profileId.isEmpty();
}

bool ApplicationLaunchRequest::valid() const
{
    if ((windowMode != QStringLiteral("main") && windowMode != QStringLiteral("detached"))
        || (!hasTarget() && windowMode != QStringLiteral("main")) || (hasTarget() && background) || !safeFile(directory)
        || !safeFile(identityFile) || !safeFile(certificateFile) || !safeFile(passwordFile) || !safeText(profileId, 256)
        || !safeText(host, 1024) || !safeText(username, 256) || !safeText(remoteDirectory, 4096) || port < 1
        || port > 65535)
        return false;
    const bool ssh = !host.isEmpty() || !profileId.isEmpty();
    if ((!directory.isEmpty() && ssh) || (!shellId.isEmpty() && (directory.isEmpty() || !validExplorerShellId(shellId)))
        || (!host.isEmpty()
            && (!profileId.isEmpty() || username.isEmpty() || host.contains(u' ') || username.contains(u' ')))
        || (!profileId.isEmpty()
            && (!username.isEmpty() || !authentication.isEmpty() || !identityFile.isEmpty()
                || !certificateFile.isEmpty() || port != 22))
        || (!ssh
            && (!username.isEmpty() || !authentication.isEmpty() || !identityFile.isEmpty()
                || !certificateFile.isEmpty() || !passwordFile.isEmpty() || !remoteDirectory.isEmpty() || port != 22)))
        return false;
    if (!host.isEmpty())
    {
        if (authentication != QStringLiteral("password") && authentication != QStringLiteral("private-key")
            && authentication != QStringLiteral("agent"))
            return false;
        if ((authentication == QStringLiteral("private-key")) != !identityFile.isEmpty()
            || (!certificateFile.isEmpty() && identityFile.isEmpty())
            || (authentication == QStringLiteral("agent") && !passwordFile.isEmpty()))
            return false;
    }
    // Credential files are local regular files, never devices, UNC paths or named pipes.
    const auto credentialPath = QDir::fromNativeSeparators(passwordFile);
    return !credentialPath.startsWith(QStringLiteral("//")) && credentialPath.indexOf(u':', 2) < 0;
}

QByteArray ApplicationLaunchRequest::toMessage() const
{
    return QJsonDocument(QJsonObject{{QStringLiteral("version"), 3},
                                     {QStringLiteral("directory"), directory},
                                     {QStringLiteral("background"), background},
                                     {QStringLiteral("shellId"), shellId},
                                     {QStringLiteral("windowMode"), windowMode},
                                     {QStringLiteral("profileId"), profileId},
                                     {QStringLiteral("host"), host},
                                     {QStringLiteral("username"), username},
                                     {QStringLiteral("port"), port},
                                     {QStringLiteral("authentication"), authentication},
                                     {QStringLiteral("identityFile"), identityFile},
                                     {QStringLiteral("certificateFile"), certificateFile},
                                     {QStringLiteral("passwordFile"), passwordFile},
                                     {QStringLiteral("remoteDirectory"), remoteDirectory}})
               .toJson(QJsonDocument::Compact)
           + '\n';
}

std::expected<ApplicationLaunchRequest, QString> ApplicationLaunchRequest::fromMessage(const QByteArray &message)
{
    if (message == "activate\n")
        return ApplicationLaunchRequest{};
    if (message.size() > 65536)
        return std::unexpected(QStringLiteral("Launch request is too large"));
    const auto document = QJsonDocument::fromJson(message);
    const auto json = document.object();
    const int version = json.value(QStringLiteral("version")).toInt(-1);
    if (!document.isObject() || json.value(QStringLiteral("version")).toDouble(-1) != version
        || (version != 1 && version != 2 && version != 3)
        || json.size()
               != (version == 1   ? 3
                   : version == 2 ? 4
                                  : 14)
        || !json.value(QStringLiteral("background")).isBool())
        return std::unexpected(QStringLiteral("Invalid launch request"));
    ApplicationLaunchRequest result;
    result.background = json.value(QStringLiteral("background")).toBool();
    const auto read = [&](const QString &key, QString &output) {
        const auto value = json.value(key);
        if (!value.isString())
            return false;
        output = value.toString();
        return true;
    };
    if (!read(QStringLiteral("directory"), result.directory)
        || (version >= 2 && !read(QStringLiteral("shellId"), result.shellId)))
        return std::unexpected(QStringLiteral("Invalid launch request"));
    if (version == 3)
    {
        if (!read(QStringLiteral("windowMode"), result.windowMode)
            || !read(QStringLiteral("profileId"), result.profileId) || !read(QStringLiteral("host"), result.host)
            || !read(QStringLiteral("username"), result.username)
            || !read(QStringLiteral("authentication"), result.authentication)
            || !read(QStringLiteral("identityFile"), result.identityFile)
            || !read(QStringLiteral("certificateFile"), result.certificateFile)
            || !read(QStringLiteral("passwordFile"), result.passwordFile)
            || !read(QStringLiteral("remoteDirectory"), result.remoteDirectory)
            || !json.value(QStringLiteral("port")).isDouble())
            return std::unexpected(QStringLiteral("Invalid launch request"));
        result.port = json.value(QStringLiteral("port")).toInt(-1);
        if (json.value(QStringLiteral("port")).toDouble() != result.port)
            return std::unexpected(QStringLiteral("Invalid launch request"));
    }
    if (!result.valid())
        return std::unexpected(QStringLiteral("Invalid launch request"));
    return result;
}
} // namespace ztermy::config
