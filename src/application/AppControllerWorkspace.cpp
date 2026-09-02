#include "application/AppController.h"

#include "domain/terminal/ShellPathQuoter.h"
#include "infrastructure/ssh/OpenSshConfigImporter.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QPointer>
#include <QThreadPool>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <ranges>
#include <string>
#include <utility>

namespace
{

[[nodiscard]] std::string utf8String(const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] QString utf8QString(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

} // namespace

namespace ztermy
{

bool AppController::insertLocalFilePath(const QString &path)
{
    const TerminalTab *tab = activeTab();
    const QFileInfo source(path);
    if (tab == nullptr || tab->kind != TerminalTabKind::Local || !tab->running || !source.exists())
    {
        return false;
    }

    terminal::ShellDialect dialect = terminal::ShellDialect::Posix;
    if (tab->localShellId.contains(QStringLiteral("powershell"), Qt::CaseInsensitive)
        || tab->localShellId.contains(QStringLiteral("pwsh"), Qt::CaseInsensitive))
    {
        dialect = terminal::ShellDialect::PowerShell;
    }
    else if (tab->localShellId.contains(QStringLiteral("cmd"), Qt::CaseInsensitive))
    {
        dialect = terminal::ShellDialect::Cmd;
    }

    const std::string localPath = utf8String(source.absoluteFilePath());
    const std::string quoted = terminal::quoteShellPath(localPath, dialect);
    return insertTerminalCommand(QString::fromUtf8(quoted.data(), static_cast<qsizetype>(quoted.size())));
}

bool AppController::importOpenSshConfig(const QString &localFileUrl)
{
    if (m_openSshImportRunning)
        return false;
    const QUrl url(localFileUrl);
    const QString requestedPath = url.isLocalFile() ? url.toLocalFile() : localFileUrl;
    const QString path =
        requestedPath.trimmed().isEmpty() ? QDir::home().filePath(QStringLiteral(".ssh/config")) : requestedPath;
    m_openSshImportRunning = true;
    m_workspaceOperationMessage = tr("Importing OpenSSH configuration...");
    emit workspaceOperationChanged();
    const QPointer<AppController> self(this);
    QThreadPool::globalInstance()->start([self, path]() noexcept {
        QVariantList values;
        QString error;
        try
        {
            const auto imported = ssh::loadOpenSshConfig(path);
            if (!imported)
                error = QStringLiteral("load-failed");
            else
            {
                values.reserve(static_cast<qsizetype>(imported->hosts.size()));
                for (const ssh::OpenSshHostConfig &host : imported->hosts)
                {
                    values.push_back(QVariantMap{{QStringLiteral("alias"), host.alias},
                                                 {QStringLiteral("hostName"), host.hostName},
                                                 {QStringLiteral("user"), host.user},
                                                 {QStringLiteral("port"), host.port},
                                                 {QStringLiteral("identityFile"), host.identityFile},
                                                 {QStringLiteral("proxyJump"), host.proxyJump}});
                }
            }
        }
        catch (...)
        {
            error = QStringLiteral("resource");
        }
        if (self)
            emit self->openSshConfigImportCompleted(std::move(values), std::move(error));
    });
    return true;
}

void AppController::applyOpenSshConfigImport(const QVariantList &hosts, const QString &error)
{
    m_openSshImportRunning = false;
    if (!error.isEmpty())
    {
        m_workspaceOperationMessage = tr("OpenSSH configuration could not be imported.");
        emit workspaceOperationChanged();
        return;
    }
    std::vector<ssh::SshProfile> candidate = m_profiles;
    QHash<QString, std::string> profileIds;
    for (const QVariant &value : hosts)
    {
        const QString alias = value.toMap().value(QStringLiteral("alias")).toString();
        const auto existing = std::ranges::find_if(candidate, [&alias](const ssh::SshProfile &profile) {
            return profile.group == "OpenSSH" && utf8QString(profile.name).compare(alias, Qt::CaseInsensitive) == 0;
        });
        profileIds.insert(alias, existing == candidate.end()
                                     ? utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces))
                                     : existing->id);
    }
    const QString defaultUser = qEnvironmentVariable("USERNAME").trimmed();
    for (const QVariant &value : hosts)
    {
        const QVariantMap imported = value.toMap();
        const QString alias = imported.value(QStringLiteral("alias")).toString();
        const std::string id = profileIds.value(alias);
        auto existing = std::ranges::find(candidate, id, &ssh::SshProfile::id);
        ssh::SshProfile profile = existing == candidate.end() ? ssh::SshProfile{} : *existing;
        profile.id = id;
        profile.name = utf8String(alias);
        profile.group = "OpenSSH";
        profile.host = utf8String(imported.value(QStringLiteral("hostName")).toString());
        profile.port = static_cast<std::uint16_t>(imported.value(QStringLiteral("port"), 22).toUInt());
        const QString importedUser = imported.value(QStringLiteral("user")).toString().trimmed();
        profile.username = utf8String(importedUser.isEmpty() ? defaultUser : importedUser);
        profile.privateKeyPath = utf8String(imported.value(QStringLiteral("identityFile")).toString());
        profile.authentication = profile.privateKeyPath.empty() ? ssh::SshAuthenticationMethod::Agent
                                                                : ssh::SshAuthenticationMethod::PrivateKey;
        profile.privateKeyPassphraseRequired = false;
        profile.identityReference.reset();
        profile.credentialReference.reset();
        profile.jumpProfileIds.clear();
        QString jump = imported.value(QStringLiteral("proxyJump")).toString();
        if (jump.contains(QLatin1Char('@')))
            jump = jump.section(QLatin1Char('@'), -1);
        jump = jump.section(QLatin1Char(':'), 0, 0);
        if (profileIds.contains(jump) && profileIds.value(jump) != profile.id)
            profile.jumpProfileIds.push_back(profileIds.value(jump));
        if (!ssh::validSshProfile(profile))
            continue;
        if (existing == candidate.end())
            candidate.push_back(std::move(profile));
        else
            *existing = std::move(profile);
    }
    if (!m_profileStore.save(candidate))
    {
        m_workspaceOperationMessage = tr("Imported OpenSSH hosts could not be saved.");
        emit workspaceOperationChanged();
        return;
    }
    m_profiles = std::move(candidate);
    m_workspaceOperationMessage = tr("Imported %n OpenSSH host(s).", "", static_cast<int>(hosts.size()));
    emit hostProfilesChanged();
    emit workspaceOperationChanged();
}

} // namespace ztermy
