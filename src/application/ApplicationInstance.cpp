#include "application/ApplicationInstance.h"
#include "core/config/ApplicationSettings.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcess>
#include <cstdlib>

namespace ztermy
{
ApplicationInstance::ApplicationInstance(QObject *parent) : QObject(parent)
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (auto *socket = m_server.nextPendingConnection())
        {
            const auto receive = [this, socket] {
                if (socket->canReadLine())
                {
                    if (socket->readLine(32) == "activate\n")
                    {
                        emit activationRequested();
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

ApplicationInstance::Result ApplicationInstance::claim(const QString &dataDirectory)
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
            socket.write("activate\n");
            socket.waitForBytesWritten(1000);
            socket.waitForReadyRead(1000);
        }
        return Result::Existing;
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
}

ApplicationInstance::Result ApplicationInstance::claimConfigured(const QString &dataDirectory,
                                                                 const QString &settingsFile)
{
    const auto settings = config::ApplicationSettingsStore(settingsFile).load();
    if (settings && !settings->windowInteraction.singleInstance)
        return Result::Primary;
    const auto result = claim(dataDirectory);
    if (result == Result::Error)
        qCritical() << "Could not acquire the application instance for this data directory";
    return result;
}

void ApplicationInstance::setWindow(QWindow *window)
{
    connect(this, &ApplicationInstance::activationRequested, window, [window] {
        if (window->windowState() == Qt::WindowMinimized)
            window->showNormal();
        else
            window->show();
        window->raise();
        window->requestActivate();
    });
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
