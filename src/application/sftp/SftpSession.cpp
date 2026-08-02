#include "application/sftp/SftpSession.h"

#include <QByteArray>
#include <QDebug>
#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <system_error>
#include <type_traits>
#include <utility>

namespace ztermy::sftp
{
namespace
{

template <typename... Visitors>
struct Overloaded : Visitors...
{
    using Visitors::operator()...;
};

std::optional<std::string> normalizedPath(const QString &path)
{
    const QByteArray bytes = path.toUtf8();
    auto normalized = normalizeRemotePath(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!normalized)
    {
        return std::nullopt;
    }
    return std::move(*normalized);
}

} // namespace

SftpSession::SftpSession(SftpClientFactory clientFactory, QObject *parent)
    : QObject(parent), m_clientFactory(std::move(clientFactory))
{
    if (!m_clientFactory)
    {
        m_clientFactory = createSftpClient;
    }
}

SftpSession::~SftpSession()
{
    stop();
}

std::error_code SftpSession::start(ssh::SshConnectionRequest request)
{
    stop();
    if (!ssh::validSshConnectionRequest(request))
    {
        request.secret.clear();
        return std::make_error_code(std::errc::invalid_argument);
    }

    {
        std::scoped_lock lock(m_commandMutex);
        m_commands.clear();
        m_latestDirectoryRequestId = 0;
        m_latestDirectoryGeneration = 0;
    }
    {
        std::scoped_lock lock(m_hostKeyMutex);
        m_hostKeyDecision.reset();
        m_awaitingHostKey = false;
    }

    m_workerFinished.store(false);
    m_worker = std::jthread([this, request = std::move(request)](const std::stop_token &stopToken) mutable {
        try
        {
            run(request, stopToken);
        }
        catch (...)
        {
            request.secret.clear();
            m_running.store(false);
            postRunning(false);
            postConnectionFailure(ssh::SshFailureKind::ProtocolError);
            postPhase(ssh::SshConnectionPhase::Failed);
        }
        m_workerFinished.store(true);
        postWorkerFinished();
    });
    return {};
}

void SftpSession::requestStop() noexcept
{
    if (!m_worker.joinable())
    {
        return;
    }
    m_worker.request_stop();
    m_commandAvailable.notify_all();
    m_hostKeyAvailable.notify_all();
}

void SftpSession::stop() noexcept
{
    if (m_worker.joinable())
    {
        requestStop();
        m_worker.join();
    }
    {
        std::scoped_lock lock(m_commandMutex);
        m_commands.clear();
    }
    if (m_running.exchange(false))
    {
        emit runningChanged(false);
    }
}

bool SftpSession::running() const noexcept
{
    return m_running.load();
}

bool SftpSession::workerFinished() const noexcept
{
    return m_workerFinished.load();
}

void SftpSession::confirmHostKey(const bool remember)
{
    {
        std::scoped_lock lock(m_hostKeyMutex);
        if (!m_awaitingHostKey)
        {
            return;
        }
        m_hostKeyDecision =
            remember ? ssh::UnknownHostKeyDecision::AcceptAndRemember : ssh::UnknownHostKeyDecision::AcceptOnce;
    }
    m_hostKeyAvailable.notify_all();
}

void SftpSession::rejectHostKey()
{
    {
        std::scoped_lock lock(m_hostKeyMutex);
        if (!m_awaitingHostKey)
        {
            return;
        }
        m_hostKeyDecision = ssh::UnknownHostKeyDecision::Reject;
    }
    m_hostKeyAvailable.notify_all();
}

void SftpSession::requestDirectory(const quint64 requestId, const quint64 generation, const QString &remotePath)
{
    auto path = normalizedPath(remotePath);
    if (!path)
    {
        postOperationFailed(requestId, SftpOperationKind::ListDirectory, ssh::SshTransportErrorKind::InvalidArgument);
        return;
    }

    std::scoped_lock lock(m_commandMutex);
    m_latestDirectoryRequestId = requestId;
    m_latestDirectoryGeneration = generation;
    std::erase_if(m_commands, [](const Command &command) {
        return std::holds_alternative<ListDirectoryCommand>(command);
    });
    m_commands.emplace_back(
        ListDirectoryCommand{.requestId = requestId, .generation = generation, .remotePath = std::move(*path)});
    m_commandAvailable.notify_all();
}

void SftpSession::requestCreateDirectory(const quint64 requestId, const QString &remotePath)
{
    auto path = normalizedPath(remotePath);
    if (!path)
    {
        postOperationFailed(requestId, SftpOperationKind::CreateDirectory, ssh::SshTransportErrorKind::InvalidArgument);
        return;
    }
    enqueue(CreateDirectoryCommand{.requestId = requestId, .remotePath = std::move(*path)});
}

void SftpSession::requestRenameEntry(const quint64 requestId, const QString &sourcePath, const QString &destinationPath)
{
    auto source = normalizedPath(sourcePath);
    auto destination = normalizedPath(destinationPath);
    if (!source || !destination || *source == *destination)
    {
        postOperationFailed(requestId, SftpOperationKind::RenameEntry, ssh::SshTransportErrorKind::InvalidArgument);
        return;
    }
    enqueue(RenameEntryCommand{.requestId = requestId,
                               .sourcePath = std::move(*source),
                               .destinationPath = std::move(*destination)});
}

void SftpSession::requestRemoveEntry(const quint64 requestId, const QString &remotePath, const bool directory)
{
    auto path = normalizedPath(remotePath);
    if (!path || *path == "/")
    {
        postOperationFailed(requestId, directory ? SftpOperationKind::RemoveDirectory : SftpOperationKind::RemoveFile,
                            ssh::SshTransportErrorKind::InvalidArgument);
        return;
    }
    enqueue(RemoveEntryCommand{.requestId = requestId, .remotePath = std::move(*path), .directory = directory});
}

void SftpSession::run(ssh::SshConnectionRequest &request, const std::stop_token &stopToken)
{
    const ssh::SshConnectionCallbacks callbacks{
        .phaseChanged =
            [this](const ssh::SshConnectionPhase phase) {
                postPhase(phase);
            },
        .confirmUnknownHostKey = [this, &stopToken](const QString &algorithm,
                                                    const QString &fingerprint) -> ssh::UnknownHostKeyDecision {
            {
                std::scoped_lock lock(m_hostKeyMutex);
                m_hostKeyDecision.reset();
                m_awaitingHostKey = true;
            }
            postHostKeyConfirmation(algorithm, fingerprint);
            std::unique_lock lock(m_hostKeyMutex);
            const bool decided = m_hostKeyAvailable.wait(lock, stopToken, [this] {
                return m_hostKeyDecision.has_value();
            });
            m_awaitingHostKey = false;
            return decided ? *m_hostKeyDecision : ssh::UnknownHostKeyDecision::Reject;
        },
        .hostKeyChanged =
            [this](const QString &algorithm, const QString &fingerprint) {
                postHostKeyChange(algorithm, fingerprint);
            },
    };

    auto client = m_clientFactory(request, callbacks, stopToken);
    request.secret.clear();
    if (!client)
    {
        postConnectionFailure(client.error().failure);
        postPhase(ssh::SshConnectionPhase::Failed);
        return;
    }
    if (stopToken.stop_requested())
    {
        return;
    }

    auto homeDirectory = (*client)->canonicalizePath(".", stopToken);
    if (!homeDirectory)
    {
        if (homeDirectory.error().kind != ssh::SshTransportErrorKind::Cancelled)
        {
            postConnectionFailure(ssh::SshFailureKind::ProtocolError);
            postPhase(ssh::SshConnectionPhase::Failed);
        }
        return;
    }

    m_running.store(true);
    postRunning(true);
    postPhase(ssh::SshConnectionPhase::Connected);
    postHomeDirectory(QString::fromUtf8(homeDirectory->data(), static_cast<qsizetype>(homeDirectory->size())));

    while (!stopToken.stop_requested())
    {
        Command command;
        {
            std::unique_lock lock(m_commandMutex);
            const bool available = m_commandAvailable.wait(lock, stopToken, [this] {
                return !m_commands.empty();
            });
            if (!available)
            {
                break;
            }
            command = std::move(m_commands.front());
            m_commands.pop_front();
        }
        processCommand(**client, std::move(command), stopToken);
    }

    m_running.store(false);
    postRunning(false);
    postPhase(ssh::SshConnectionPhase::Disconnected);
}

void SftpSession::processCommand(SftpClient &client, Command command, const std::stop_token &stopToken)
{
    std::visit(
        Overloaded{
            [this, &client, &stopToken](ListDirectoryCommand &list) {
                if (!isCurrentDirectoryRequest(list.requestId, list.generation))
                {
                    return;
                }
                auto result = client.listDirectory(list.remotePath, stopToken);
                if (!result)
                {
                    if (isCurrentDirectoryRequest(list.requestId, list.generation))
                    {
                        postOperationFailed(list.requestId, SftpOperationKind::ListDirectory, result.error().kind);
                    }
                    return;
                }
                if (isCurrentDirectoryRequest(list.requestId, list.generation))
                {
                    postDirectory(
                        list.requestId, list.generation,
                        QString::fromUtf8(list.remotePath.data(), static_cast<qsizetype>(list.remotePath.size())),
                        std::make_shared<const DirectoryListing>(std::move(*result)));
                }
            },
            [this, &client, &stopToken](const CreateDirectoryCommand &create) {
                auto result = client.createDirectory(create.remotePath, stopToken);
                result ? postOperationSucceeded(create.requestId, SftpOperationKind::CreateDirectory)
                       : postOperationFailed(create.requestId, SftpOperationKind::CreateDirectory, result.error().kind);
            },
            [this, &client, &stopToken](const RenameEntryCommand &rename) {
                auto result = client.renameEntry(rename.sourcePath, rename.destinationPath, false, stopToken);
                result ? postOperationSucceeded(rename.requestId, SftpOperationKind::RenameEntry)
                       : postOperationFailed(rename.requestId, SftpOperationKind::RenameEntry, result.error().kind);
            },
            [this, &client, &stopToken](const RemoveEntryCommand &remove) {
                const auto operation =
                    remove.directory ? SftpOperationKind::RemoveDirectory : SftpOperationKind::RemoveFile;
                auto result = client.removeEntry(remove.remotePath, remove.directory, stopToken);
                result ? postOperationSucceeded(remove.requestId, operation)
                       : postOperationFailed(remove.requestId, operation, result.error().kind);
            }},
        command);
}

void SftpSession::enqueue(Command command)
{
    {
        std::scoped_lock lock(m_commandMutex);
        m_commands.push_back(std::move(command));
    }
    m_commandAvailable.notify_all();
}

bool SftpSession::isCurrentDirectoryRequest(const quint64 requestId, const quint64 generation) const noexcept
{
    std::scoped_lock lock(m_commandMutex);
    return requestId == m_latestDirectoryRequestId && generation == m_latestDirectoryGeneration;
}

void SftpSession::postRunning(const bool running)
{
    if (QThread::currentThread() == thread())
    {
        deliverRunning(running);
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverRunning", Qt::QueuedConnection, Q_ARG(bool, running)))
    {
        qWarning("SFTP running state could not be queued to its owner thread");
    }
}

void SftpSession::deliverRunning(const bool running)
{
    emit runningChanged(running);
}

void SftpSession::postWorkerFinished()
{
    if (QThread::currentThread() == thread())
    {
        deliverWorkerFinished();
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverWorkerFinished", Qt::QueuedConnection))
    {
        qWarning("SFTP worker completion could not be queued to its owner thread");
    }
}

void SftpSession::deliverWorkerFinished()
{
    emit workerFinishedChanged();
}

void SftpSession::postHomeDirectory(const QString &remotePath)
{
    if (QThread::currentThread() == thread())
    {
        deliverHomeDirectory(remotePath);
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverHomeDirectory", Qt::QueuedConnection, Q_ARG(QString, remotePath)))
    {
        qWarning("SFTP home directory could not be queued to its owner thread");
    }
}

void SftpSession::deliverHomeDirectory(const QString &remotePath)
{
    emit homeDirectoryReady(remotePath);
}

void SftpSession::postPhase(const ssh::SshConnectionPhase phase)
{
    if (QThread::currentThread() == thread())
    {
        deliverPhase(phase);
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverPhase", Qt::QueuedConnection,
                                   Q_ARG(ztermy::ssh::SshConnectionPhase, phase)))
    {
        qWarning("SFTP SSH phase could not be queued to its owner thread");
    }
}

void SftpSession::deliverPhase(const ssh::SshConnectionPhase phase)
{
    emit sshPhaseChanged(phase);
}

void SftpSession::postConnectionFailure(const ssh::SshFailureKind failure)
{
    if (QThread::currentThread() == thread())
    {
        deliverConnectionFailure(failure);
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverConnectionFailure", Qt::QueuedConnection,
                                   Q_ARG(ztermy::ssh::SshFailureKind, failure)))
    {
        qWarning("SFTP connection failure could not be queued to its owner thread");
    }
}

void SftpSession::deliverConnectionFailure(const ssh::SshFailureKind failure)
{
    emit connectionFailed(failure);
}

void SftpSession::postHostKeyConfirmation(const QString &algorithm, const QString &fingerprint)
{
    if (QThread::currentThread() == thread())
    {
        deliverHostKeyConfirmation(algorithm, fingerprint);
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverHostKeyConfirmation", Qt::QueuedConnection, Q_ARG(QString, algorithm),
                                   Q_ARG(QString, fingerprint)))
    {
        qWarning("SFTP host-key confirmation could not be queued to its owner thread");
    }
}

void SftpSession::deliverHostKeyConfirmation(const QString &algorithm, const QString &fingerprint)
{
    emit hostKeyConfirmationRequired(algorithm, fingerprint);
}

void SftpSession::postHostKeyChange(const QString &algorithm, const QString &fingerprint)
{
    if (QThread::currentThread() == thread())
    {
        deliverHostKeyChange(algorithm, fingerprint);
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverHostKeyChange", Qt::QueuedConnection, Q_ARG(QString, algorithm),
                                   Q_ARG(QString, fingerprint)))
    {
        qWarning("SFTP host-key change could not be queued to its owner thread");
    }
}

void SftpSession::deliverHostKeyChange(const QString &algorithm, const QString &fingerprint)
{
    emit hostKeyChanged(algorithm, fingerprint);
}

void SftpSession::postDirectory(const quint64 requestId, const quint64 generation, const QString &remotePath,
                                DirectoryListingPtr entries)
{
    if (QThread::currentThread() == thread())
    {
        deliverDirectory(requestId, generation, remotePath, std::move(entries));
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverDirectory", Qt::QueuedConnection, Q_ARG(quint64, requestId),
                                   Q_ARG(quint64, generation), Q_ARG(QString, remotePath),
                                   Q_ARG(ztermy::sftp::DirectoryListingPtr, entries)))
    {
        qWarning("SFTP directory result could not be queued to its owner thread");
    }
}

void SftpSession::deliverDirectory(const quint64 requestId, const quint64 generation, const QString &remotePath,
                                   DirectoryListingPtr entries)
{
    emit directoryReady(requestId, generation, remotePath, std::move(entries));
}

void SftpSession::postOperationSucceeded(const quint64 requestId, const SftpOperationKind operation)
{
    if (QThread::currentThread() == thread())
    {
        deliverOperationSucceeded(requestId, operation);
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverOperationSucceeded", Qt::QueuedConnection, Q_ARG(quint64, requestId),
                                   Q_ARG(ztermy::sftp::SftpOperationKind, operation)))
    {
        qWarning("SFTP operation result could not be queued to its owner thread");
    }
}

void SftpSession::deliverOperationSucceeded(const quint64 requestId, const SftpOperationKind operation)
{
    emit operationSucceeded(requestId, operation);
}

void SftpSession::postOperationFailed(const quint64 requestId, const SftpOperationKind operation,
                                      const ssh::SshTransportErrorKind error)
{
    if (QThread::currentThread() == thread())
    {
        deliverOperationFailed(requestId, operation, error);
        return;
    }
    if (!QMetaObject::invokeMethod(this, "deliverOperationFailed", Qt::QueuedConnection, Q_ARG(quint64, requestId),
                                   Q_ARG(ztermy::sftp::SftpOperationKind, operation),
                                   Q_ARG(ztermy::ssh::SshTransportErrorKind, error)))
    {
        qWarning("SFTP operation failure could not be queued to its owner thread");
    }
}

void SftpSession::deliverOperationFailed(const quint64 requestId, const SftpOperationKind operation,
                                         const ssh::SshTransportErrorKind error)
{
    emit operationFailed(requestId, operation, error);
}

} // namespace ztermy::sftp
