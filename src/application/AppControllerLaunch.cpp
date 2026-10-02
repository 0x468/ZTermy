#include "application/AppController.h"

#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <algorithm>

namespace ztermy
{
bool AppController::openLaunchRequest(const config::ApplicationLaunchRequest &request)
{
    if (m_shutdownStarted || !request.valid() || !request.hasTarget() || m_launchQueue.size() >= 32)
        return false;
    m_launchQueue.push_back(request);
    processNextLaunch();
    return true;
}

void AppController::processNextLaunch()
{
    if (m_shutdownStarted || m_launchBusy || m_pendingLaunch || m_launchQueue.isEmpty())
        return;
    const auto launch = m_launchQueue.takeFirst();
    const bool detached = launch.windowMode == QStringLiteral("detached");
    if (!launch.directory.isEmpty())
    {
        const auto shellId = launch.shellId.isEmpty() ? m_settings.windowsIntegration.singleShell : launch.shellId;
        if (shellId != QStringLiteral("automatic")
            && std::ranges::none_of(m_localShellProfiles, [&](const auto &shell) {
                   return shell.id == shellId && shell.available;
               }))
        {
            emit launchFailed(tr("The requested local Shell is not available on this computer."));
            QTimer::singleShot(0, this, &AppController::processNextLaunch);
            return;
        }
        // Do not publish the legacy main-window selection signal before detaching.
        const auto id =
            startLocalTerminalAt(launch.directory, {}, shellId == QStringLiteral("automatic") ? QString{} : shellId);
        if (id.isEmpty() || (detached && !detachTerminalWorkspace(id)))
            emit launchFailed(tr("The requested terminal could not be opened."));
        else
            emit launchOpened(id, detached);
        QTimer::singleShot(0, this, &AppController::processNextLaunch);
        return;
    }
    if (!launch.passwordFile.isEmpty())
    {
        m_launchBusy = true;
        m_launchWorker.start([this, launch] {
            auto secret = std::make_shared<security::SensitiveByteArray>();
            QFile file(launch.passwordFile);
            bool readable = QFileInfo(file).isFile() && file.open(QIODevice::ReadOnly);
            if (readable)
            {
                QByteArray bytes = file.read(4097);
                readable = file.error() == QFile::NoError && bytes.size() <= 4096 && file.atEnd();
                if (bytes.endsWith('\n'))
                    bytes.chop(1);
                if (bytes.endsWith('\r'))
                    bytes.chop(1);
                readable = readable && !bytes.isEmpty() && !bytes.contains('\0') && !bytes.contains('\n')
                           && !bytes.contains('\r');
                *secret = security::SensitiveByteArray(std::move(bytes));
            }
            emit launchCredentialReadCompleted(launch, secret, readable);
        });
        return;
    }
    if (!launch.profileId.isEmpty() || launch.authentication == QStringLiteral("agent"))
    {
        if (startLaunchConnection(launch, {}))
        {
            QTimer::singleShot(0, this, &AppController::processNextLaunch);
            return;
        }
        if (launch.authentication == QStringLiteral("agent"))
        {
            emit launchFailed(tr("The requested SSH connection could not be started."));
            QTimer::singleShot(0, this, &AppController::processNextLaunch);
            return;
        }
        if (!launch.profileId.isEmpty() && std::ranges::none_of(m_profiles, [&](const auto &profile) {
                return QString::fromStdString(profile.id) == launch.profileId;
            }))
        {
            emit launchFailed(tr("The requested saved host no longer exists."));
            QTimer::singleShot(0, this, &AppController::processNextLaunch);
            return;
        }
    }
    m_pendingLaunch = launch;
    emit launchAuthenticationRequested(
        {{QStringLiteral("target"), launch.profileId.isEmpty()
                                        ? QStringLiteral("%1@%2:%3").arg(launch.username, launch.host).arg(launch.port)
                                        : launch.profileId},
         {QStringLiteral("privateKey"), !launch.identityFile.isEmpty()},
         {QStringLiteral("savedProfile"), !launch.profileId.isEmpty()}});
}

void AppController::finishLaunchCredentialRead(const config::ApplicationLaunchRequest &launch,
                                               const std::shared_ptr<security::SensitiveByteArray> &secret,
                                               const bool readable)
{
    m_launchBusy = false;
    if (m_shutdownStarted)
        return;
    if (!readable)
        emit launchFailed(tr("The credential file must be a readable local file containing one line, "
                             "at most 4096 bytes."));
    else if (!startLaunchConnection(launch, std::move(*secret)))
        emit launchFailed(tr("The requested SSH connection could not be started."));
    processNextLaunch();
}

bool AppController::completeLaunchAuthentication(const QString &secret, const QString &proxySecret,
                                                 const bool cancelled)
{
    if (m_shutdownStarted || !m_pendingLaunch)
        return false;
    if (!cancelled
        && !startLaunchConnection(*m_pendingLaunch, security::SensitiveByteArray(secret.toUtf8()), proxySecret))
        return false;
    m_pendingLaunch.reset();
    QTimer::singleShot(0, this, &AppController::processNextLaunch);
    return true;
}

bool AppController::startLaunchConnection(const config::ApplicationLaunchRequest &launch,
                                          security::SensitiveByteArray secret, const QString &proxySecret)
{
    std::optional<ssh::SshConnectionRequest> request;
    if (!launch.profileId.isEmpty())
    {
        const auto found = std::ranges::find(m_profiles, launch.profileId.toStdString(), &ssh::SshProfile::id);
        if (found == m_profiles.end())
            return false;
        request = connectionRequestForProfileBytes(*found, std::move(secret), proxySecret);
    }
    else
    {
        request = ssh::SshConnectionRequest{.host = launch.host,
                                            .port = static_cast<std::uint16_t>(launch.port),
                                            .username = launch.username,
                                            .authentication = launch.authentication == QStringLiteral("agent")
                                                                  ? ssh::SshAuthenticationMethod::Agent
                                                              : launch.authentication == QStringLiteral("private-key")
                                                                  ? ssh::SshAuthenticationMethod::PrivateKey
                                                                  : ssh::SshAuthenticationMethod::Password,
                                            .privateKeyPath = launch.identityFile,
                                            .publicKeyPath = launch.certificateFile,
                                            .secret = std::move(secret),
                                            .knownHostsPath = m_knownHostsPath};
    }
    if (!request || !ssh::validSshConnectionRequest(*request))
        return false;
    if (!launch.remoteDirectory.isEmpty())
    {
        auto quoted = launch.remoteDirectory;
        quoted.replace(u'\'', QStringLiteral("'\\''"));
        request->sessionOptions.startupCommand =
            (QStringLiteral("cd -- '") + quoted + QStringLiteral("'\n")).toStdString()
            + request->sessionOptions.startupCommand;
    }
    if (!startSshConnection(std::move(*request), launch.profileId))
        return false;
    const auto id = activeTerminalTabId();
    const bool detached = launch.windowMode == QStringLiteral("detached");
    if (detached && !detachTerminalWorkspace(id))
    {
        emit launchFailed(tr("The terminal opened, but it could not be moved to a separate window."));
        return true;
    }
    emit launchOpened(id, detached);
    return true;
}
} // namespace ztermy
