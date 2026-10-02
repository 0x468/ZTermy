#include "application/ApplicationInstance.h"
#include "core/config/ApplicationSettings.h"
#include "core/windowing/WindowPresenter.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcess>
#include <QTimer>
#include <qt_windows.h>
#include <cstdlib>
#include <utility>

namespace ztermy
{
ApplicationInstance::ApplicationInstance(QObject *parent) : QObject(parent)
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (auto *socket = m_server.nextPendingConnection())
        {
            socket->setReadBufferSize(65537);
            QTimer::singleShot(5000, socket, [socket] {
                socket->disconnectFromServer();
            });
            const auto receive = [this, socket] {
                if (socket->bytesAvailable() > 65536)
                {
                    socket->disconnectFromServer();
                    return;
                }
                if (socket->canReadLine())
                {
                    const auto request = config::ApplicationLaunchRequest::fromMessage(socket->readLine(65537));
                    if (request && m_pendingRequests.size() < 32)
                    {
                        if (m_ready)
                            dispatch(*request);
                        else
                            m_pendingRequests.push_back(*request);
                        socket->write("ok\n");
                        socket->flush();
                    }
                    socket->disconnectFromServer();
                }
            };
            connect(socket, &QLocalSocket::readyRead, this, receive);
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            receive();
        }
    });
}

ApplicationInstance::Result ApplicationInstance::claim(const QString &dataDirectory,
                                                       const config::ApplicationLaunchRequest &request)
{
    const QString canonical = QFileInfo(dataDirectory).canonicalFilePath();
    if (canonical.isEmpty())
        return Result::Error;
    const auto digest = QCryptographicHash::hash(canonical.toCaseFolded().toUtf8(), QCryptographicHash::Sha256).toHex();
    const QString serverName = QStringLiteral("ztermy-") + QString::fromLatin1(digest);
    m_lock = std::make_unique<QLockFile>(QDir(canonical).filePath(QStringLiteral("instance.lock")));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock())
    {
        if (m_lock->error() != QLockFile::LockFailedError)
            return Result::Error;
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (socket.waitForConnected(1000))
        {
            ULONG serverPid = 0;
            // QLocalSocket exposes the Win32 pipe HANDLE through qintptr.
            const auto pipe = reinterpret_cast<HANDLE>(socket.socketDescriptor()); // NOLINT(performance-no-int-to-ptr)
            if (GetNamedPipeServerProcessId(pipe, &serverPid))
                AllowSetForegroundWindow(serverPid); // Transfer the Explorer/user launch foreground grant.
            socket.write(request.toMessage());
            if (socket.waitForBytesWritten(1000) && socket.waitForReadyRead(10000) && socket.readLine(32) == "ok\n")
                return Result::Existing;
        }
        return Result::Error;
    }
    // Only the lock owner can remove a stale endpoint after an abnormal exit.
    QLocalServer::removeServer(serverName);
    if (!m_server.listen(serverName))
    {
        release();
        return Result::Error;
    }
    return Result::Primary;
}

void ApplicationInstance::release()
{
    m_server.close();
    m_lock.reset();
    m_ready = false;
    m_pendingRequests.clear();
}

ApplicationInstance::Result ApplicationInstance::claimConfigured(const QString &dataDirectory,
                                                                 const QString &settingsFile,
                                                                 const config::ApplicationLaunchRequest &request)
{
    const auto settings = config::ApplicationSettingsStore(settingsFile).load();
    if (settings && !settings->windowInteraction.singleInstance)
        return Result::Primary;
    const auto result = claim(dataDirectory, request);
    if (result == Result::Error)
        qCritical() << "Could not acquire the application instance for this data directory";
    return result;
}

void ApplicationInstance::setWindow(QWindow *window)
{
    connect(this, &ApplicationInstance::activationRequested, window, [window] {
        windowing::present(*window);
    });
}

void ApplicationInstance::dispatch(const config::ApplicationLaunchRequest &request)
{
    if (!request.directory.isEmpty())
        emit directoryOpenRequested(request.directory, request.shellId);
    if (!request.background)
        emit activationRequested();
}

void ApplicationInstance::setReady()
{
    m_ready = true;
    const auto requests = std::exchange(m_pendingRequests, {});
    for (const auto &request : requests)
        dispatch(request);
}

void ApplicationInstance::requestRestart()
{
    m_restartRequested = true;
    QCoreApplication::quit();
}

int ApplicationInstance::finish(const int exitCode)
{
    if (m_restartRequested)
    {
        release();
        if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().sliced(1)))
        {
            qCritical() << "Could not restart ztermy";
            return EXIT_FAILURE;
        }
    }
    return exitCode;
}
} // namespace ztermy
