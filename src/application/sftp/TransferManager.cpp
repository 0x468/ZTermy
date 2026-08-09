#include "application/sftp/TransferManager.h"
#include "application/sftp/SftpFilenameCodec.h"

#include "infrastructure/sftp/TransferRecoveryStore.h"

#include <QDateTime>
#include <QFile>
#include <QMetaObject>

#include <system_error>
#include <utility>

namespace ztermy::sftp
{
namespace
{

std::string credentialErrorCode(const TransferCredentialError error)
{
    switch (error)
    {
        case TransferCredentialError::Locked:
            return "credential-locked";
        case TransferCredentialError::Unavailable:
            return "credential-unavailable";
        case TransferCredentialError::Cancelled:
            return "credential-cancelled";
    }
    return "credential-unavailable";
}

std::string executionErrorCode(const TransferExecutionResult &result)
{
    if (result.transportError)
    {
        switch (result.transportError->kind)
        {
            case ssh::SshTransportErrorKind::TimedOut:
                return "remote-timeout";
            case ssh::SshTransportErrorKind::ConnectionLost:
                return "remote-connection-lost";
            case ssh::SshTransportErrorKind::AuthenticationRejected:
                return "authentication-rejected";
            case ssh::SshTransportErrorKind::AuthenticationUnavailable:
                return "authentication-unavailable";
            case ssh::SshTransportErrorKind::Cancelled:
                return "cancelled";
            case ssh::SshTransportErrorKind::InitializationFailed:
            case ssh::SshTransportErrorKind::InvalidArgument:
            case ssh::SshTransportErrorKind::InvalidState:
            case ssh::SshTransportErrorKind::ProtocolError:
                return "remote-io";
        }
    }
    if (!result.error)
    {
        return "transfer-failed";
    }
    switch (*result.error)
    {
        case TransferExecutionErrorKind::InvalidTask:
            return "invalid-task";
        case TransferExecutionErrorKind::LocalIo:
            return "local-io";
        case TransferExecutionErrorKind::RemoteIo:
            return "remote-io";
        case TransferExecutionErrorKind::IncompatibleConflict:
            return "incompatible-conflict";
        case TransferExecutionErrorKind::InvalidConflictRename:
            return "invalid-conflict-rename";
        case TransferExecutionErrorKind::CommitFailed:
            return "commit-failed";
    }
    return "transfer-failed";
}

} // namespace

TransferManager::TransferManager(const std::size_t concurrencyLimit, SftpClientFactory clientFactory, QObject *parent)
    : QObject(parent), m_queue(concurrencyLimit), m_clientFactory(std::move(clientFactory))
{
    if (!m_clientFactory)
    {
        m_clientFactory = createSftpClient;
    }
}

TransferManager::~TransferManager()
{
    requestStop();
    m_workers.clear();
    m_cleanupWorkers.clear();
}

void TransferManager::requestStop() noexcept
{
    if (m_stopRequested)
    {
        return;
    }
    m_stopRequested = true;
    for (auto &[id, worker] : m_workers)
    {
        (void)id;
        worker.thread.request_stop();
        worker.context->hostKeyAvailable.notify_all();
    }
}

void TransferManager::shutdown() noexcept
{
    if (m_shutdownComplete)
    {
        return;
    }
    requestStop();
    m_workers.clear();
    m_cleanupWorkers.clear();
    persistRecovery();
    m_shutdownComplete = true;
}

std::expected<void, TransferQueueError>
TransferManager::enqueue(TransferTask task, TransferRequestProvider requestProvider, TransferExecutionOptions options)
{
    if (m_stopRequested || !requestProvider)
    {
        return std::unexpected(TransferQueueError::InvalidTask);
    }
    const std::string id = task.id;
    auto queued = m_queue.enqueue(std::move(task));
    if (!queued)
    {
        return queued;
    }
    m_work.emplace(id, WorkSpec{.requestProvider = std::move(requestProvider), .options = std::move(options)});
    publishSnapshot();
    schedule();
    return {};
}

TransferTasksPtr TransferManager::snapshot() const
{
    return std::make_shared<const std::vector<TransferTask>>(m_queue.tasks());
}

void TransferManager::enableRecovery(QString path, TransferRecoveryRequestProviderFactory requestProviderFactory)
{
    if (m_stopRequested)
    {
        return;
    }
    m_recoveryStore = std::make_unique<TransferRecoveryStore>(std::move(path));
    m_recoveryRequestProviderFactory = std::move(requestProviderFactory);
    auto recovered = m_recoveryStore->load();
    if (!recovered)
    {
        emit recoveryError(QStringLiteral("recovery-load-failed"));
        return;
    }
    if (!m_recoveryRequestProviderFactory)
    {
        emit recoveryError(QStringLiteral("recovery-load-failed"));
        return;
    }
    for (TransferTask &task : *recovered)
    {
        const std::string id = task.id;
        auto restored = m_queue.restoreInterrupted(std::move(task));
        if (!restored)
        {
            emit recoveryError(QStringLiteral("recovery-load-failed"));
            continue;
        }
        const TransferTask *restoredTask = m_queue.find(id);
        Q_ASSERT(restoredTask != nullptr);
        m_work.emplace(id, WorkSpec{.requestProvider = m_recoveryRequestProviderFactory(restoredTask->endpointId)});
    }
    publishSnapshot();
}

void TransferManager::cancel(const QString &taskIdentifier)
{
    const std::string id = taskId(taskIdentifier);
    const TransferTask *task = m_queue.find(id);
    const bool wasRunning = task != nullptr && task->status == TransferStatus::Running;
    if (auto worker = m_workers.find(id); worker != m_workers.end())
    {
        worker->second.context->pauseRequested = false;
        worker->second.thread.request_stop();
        worker->second.context->hostKeyAvailable.notify_all();
    }
    if (m_queue.cancel(id, QDateTime::currentMSecsSinceEpoch()))
    {
        m_work.erase(id);
        publishSnapshot();
        if (!wasRunning)
        {
            schedule();
        }
    }
}

void TransferManager::pause(const QString &taskIdentifier)
{
    const std::string id = taskId(taskIdentifier);
    const TransferTask *task = m_queue.find(id);
    const bool wasRunning = task != nullptr && task->status == TransferStatus::Running;
    if (!m_queue.pause(id))
    {
        return;
    }
    if (auto worker = m_workers.find(id); worker != m_workers.end())
    {
        worker->second.context->pauseRequested = true;
        worker->second.thread.request_stop();
        worker->second.context->hostKeyAvailable.notify_all();
    }
    publishSnapshot();
    if (!wasRunning)
    {
        schedule();
    }
}

void TransferManager::resume(const QString &taskIdentifier)
{
    const std::string id = taskId(taskIdentifier);
    if (m_work.contains(id) && m_queue.resume(id))
    {
        publishSnapshot();
        schedule();
    }
}

void TransferManager::retry(const QString &taskIdentifier)
{
    const std::string id = taskId(taskIdentifier);
    if (m_work.contains(id) && m_queue.retry(id))
    {
        publishSnapshot();
        schedule();
    }
}

void TransferManager::dismiss(const QString &taskIdentifier)
{
    const std::string id = taskId(taskIdentifier);
    const TransferTask *task = m_queue.find(id);
    const auto work = m_work.find(id);
    const std::optional<TransferTask> cleanupTask = task == nullptr ? std::nullopt : std::optional(*task);
    const std::optional<WorkSpec> cleanupWork = work == m_work.end() ? std::nullopt : std::optional(work->second);
    if (m_queue.dismiss(id))
    {
        m_work.erase(id);
        if (cleanupTask && cleanupWork)
        {
            cleanupPartial(*cleanupTask, *cleanupWork);
        }
        publishSnapshot();
    }
}

void TransferManager::dismissFinished()
{
    std::vector<std::pair<TransferTask, WorkSpec>> cleanup;
    for (const TransferTask &task : m_queue.tasks())
    {
        if (task.status != TransferStatus::Completed && task.status != TransferStatus::Failed
            && task.status != TransferStatus::Cancelled)
        {
            continue;
        }
        const auto work = m_work.find(task.id);
        if (work != m_work.end())
        {
            cleanup.emplace_back(task, work->second);
        }
    }
    if (m_queue.dismissFinished() > 0)
    {
        for (const auto &[task, work] : cleanup)
        {
            cleanupPartial(task, work);
        }
        std::erase_if(m_work, [this](const auto &item) {
            return m_queue.find(item.first) == nullptr;
        });
        publishSnapshot();
    }
}

void TransferManager::pauseAll()
{
    std::vector<QString> identifiers;
    for (const TransferTask &task : m_queue.tasks())
    {
        if (task.status == TransferStatus::Queued || task.status == TransferStatus::Running)
        {
            identifiers.push_back(qTaskId(task.id));
        }
    }
    for (const QString &identifier : identifiers)
    {
        pause(identifier);
    }
}

void TransferManager::resumeAll()
{
    std::vector<QString> identifiers;
    for (const TransferTask &task : m_queue.tasks())
    {
        if (task.status == TransferStatus::Paused)
        {
            identifiers.push_back(qTaskId(task.id));
        }
    }
    for (const QString &identifier : identifiers)
    {
        resume(identifier);
    }
}

void TransferManager::cancelAll()
{
    std::vector<QString> identifiers;
    for (const TransferTask &task : m_queue.tasks())
    {
        if (task.status == TransferStatus::Queued || task.status == TransferStatus::Running
            || task.status == TransferStatus::Pausing || task.status == TransferStatus::Paused
            || task.status == TransferStatus::NeedsAttention)
        {
            identifiers.push_back(qTaskId(task.id));
        }
    }
    for (const QString &identifier : identifiers)
    {
        cancel(identifier);
    }
}

void TransferManager::resolveConflict(const QString &taskIdentifier, const ConflictAction action,
                                      const QString &renamedDestinationPath)
{
    const std::string id = taskId(taskIdentifier);
    const auto work = m_work.find(id);
    if (work == m_work.end())
    {
        return;
    }
    work->second.options.conflictAction = action;
    work->second.options.renamedDestinationPath = taskId(renamedDestinationPath);
    if (m_queue.retry(id))
    {
        publishSnapshot();
        schedule();
    }
}

void TransferManager::confirmHostKey(const QString &taskIdentifier, const bool remember)
{
    const auto worker = m_workers.find(taskId(taskIdentifier));
    if (worker == m_workers.end())
    {
        return;
    }
    {
        std::scoped_lock lock(worker->second.context->mutex);
        if (!worker->second.context->awaitingHostKey)
        {
            return;
        }
        worker->second.context->hostKeyDecision =
            remember ? ssh::UnknownHostKeyDecision::AcceptAndRemember : ssh::UnknownHostKeyDecision::AcceptOnce;
    }
    worker->second.context->hostKeyAvailable.notify_all();
}

void TransferManager::rejectHostKey(const QString &taskIdentifier)
{
    const auto worker = m_workers.find(taskId(taskIdentifier));
    if (worker == m_workers.end())
    {
        return;
    }
    {
        std::scoped_lock lock(worker->second.context->mutex);
        if (!worker->second.context->awaitingHostKey)
        {
            return;
        }
        worker->second.context->hostKeyDecision = ssh::UnknownHostKeyDecision::Reject;
    }
    worker->second.context->hostKeyAvailable.notify_all();
}

void TransferManager::schedule()
{
    if (m_stopRequested)
    {
        return;
    }
    while (m_queue.runningCount() < m_queue.concurrencyLimit())
    {
        auto next = m_queue.takeNext(QDateTime::currentMSecsSinceEpoch());
        if (!next)
        {
            break;
        }
        const auto work = m_work.find(next->id);
        if (work == m_work.end())
        {
            [[maybe_unused]] const auto failed =
                m_queue.fail(next->id, "request-provider-missing", false, QDateTime::currentMSecsSinceEpoch());
            continue;
        }
        try
        {
            startWorker(*next, work->second);
        }
        catch (...)
        {
            [[maybe_unused]] const auto failed =
                m_queue.fail(next->id, "worker-initialization-failed", true, QDateTime::currentMSecsSinceEpoch());
        }
    }
    publishSnapshot();
}

void TransferManager::startWorker(const TransferTask &task, const WorkSpec &spec)
{
    const std::string id = task.id;
    const auto context = std::make_shared<WorkerContext>();
    const auto input = std::make_shared<WorkerInput>(WorkerInput{.task = task, .spec = spec});
    std::jthread worker([this, input, context](const std::stop_token &stopToken) mutable {
        try
        {
            const TransferTask &task = input->task;
            WorkSpec &spec = input->spec;
            auto request = spec.requestProvider();
            if (!request)
            {
                QMetaObject::invokeMethod(this, "deliverCredentialError", Qt::QueuedConnection,
                                          Q_ARG(QString, qTaskId(task.id)),
                                          Q_ARG(ztermy::sftp::TransferCredentialError, request.error()));
                return;
            }
            const ssh::SshConnectionCallbacks callbacks{
                .phaseChanged = {},
                .confirmUnknownHostKey = [this, id = task.id, context,
                                          &stopToken](const QString &algorithm,
                                                      const QString &fingerprint) -> ssh::UnknownHostKeyDecision {
                    {
                        std::scoped_lock lock(context->mutex);
                        context->hostKeyDecision.reset();
                        context->awaitingHostKey = true;
                    }
                    QMetaObject::invokeMethod(this, "deliverHostKeyConfirmation", Qt::QueuedConnection,
                                              Q_ARG(QString, qTaskId(id)), Q_ARG(QString, algorithm),
                                              Q_ARG(QString, fingerprint));
                    std::unique_lock lock(context->mutex);
                    const bool decided = context->hostKeyAvailable.wait(lock, stopToken, [context] {
                        return context->hostKeyDecision.has_value();
                    });
                    context->awaitingHostKey = false;
                    return decided ? *context->hostKeyDecision : ssh::UnknownHostKeyDecision::Reject;
                },
                .hostKeyChanged =
                    [this, id = task.id](const QString &algorithm, const QString &fingerprint) {
                        QMetaObject::invokeMethod(this, "deliverHostKeyChange", Qt::QueuedConnection,
                                                  Q_ARG(QString, qTaskId(id)), Q_ARG(QString, algorithm),
                                                  Q_ARG(QString, fingerprint));
                    },
            };
            auto client = m_clientFactory(*request, callbacks, stopToken);
            request->secret.clear();
            if (!client)
            {
                TransferExecutionResult result{
                    .kind = client.error().failure == ssh::SshFailureKind::Cancelled
                                ? TransferExecutionResultKind::Cancelled
                                : TransferExecutionResultKind::Failed,
                    .error = TransferExecutionErrorKind::RemoteIo,
                };
                QMetaObject::invokeMethod(this, "deliverResult", Qt::QueuedConnection, Q_ARG(QString, qTaskId(task.id)),
                                          Q_ARG(ztermy::sftp::TransferExecutionResult, result));
                return;
            }
            *client = withFilenameEncoding(std::move(*client), task.filenameEncoding);
            auto result = executeTransfer(
                **client, task, spec.options,
                [this, id = task.id](const std::uint64_t transferred, const std::uint64_t total,
                                     const std::uint64_t speed) {
                    QMetaObject::invokeMethod(this, "deliverProgress", Qt::QueuedConnection,
                                              Q_ARG(QString, qTaskId(id)), Q_ARG(qulonglong, transferred),
                                              Q_ARG(qulonglong, total), Q_ARG(qulonglong, speed));
                },
                stopToken,
                [context] {
                    return context->pauseRequested.load();
                });
            QMetaObject::invokeMethod(this, "deliverResult", Qt::QueuedConnection, Q_ARG(QString, qTaskId(task.id)),
                                      Q_ARG(ztermy::sftp::TransferExecutionResult, result));
        }
        catch (...)
        {
            const TransferExecutionResult result{.kind = TransferExecutionResultKind::Failed,
                                                 .error = TransferExecutionErrorKind::RemoteIo};
            QMetaObject::invokeMethod(this, "deliverResult", Qt::QueuedConnection,
                                      Q_ARG(QString, qTaskId(input->task.id)),
                                      Q_ARG(ztermy::sftp::TransferExecutionResult, result));
        }
    });
    m_workers.emplace(id, WorkerRecord{.context = context, .thread = std::move(worker)});
}

void TransferManager::cleanupPartial(const TransferTask &task, const WorkSpec &spec)
{
    const std::string destination =
        spec.options.conflictAction == ConflictAction::Rename && !spec.options.renamedDestinationPath.empty()
            ? spec.options.renamedDestinationPath
            : task.destinationPath;
    if (task.direction == TransferDirection::Download)
    {
        QFile::remove(localTransferPartialPath(destination, task.id));
        return;
    }

    pruneCleanupWorkers();
    const auto finished = std::make_shared<std::atomic_bool>(false);
    const auto factory = std::make_shared<SftpClientFactory>(m_clientFactory);
    const auto input = std::make_shared<WorkerInput>(WorkerInput{.task = task, .spec = spec});
    const auto cleanupDestination = std::make_shared<std::string>(destination);
    std::jthread worker([factory, finished, input, cleanupDestination](const std::stop_token &stopToken) noexcept {
        try
        {
            do
            {
                auto request = input->spec.requestProvider();
                if (!request)
                {
                    break;
                }
                const ssh::SshConnectionCallbacks callbacks{
                    .phaseChanged = {},
                    .confirmUnknownHostKey =
                        [](const QString &, const QString &) {
                            return ssh::UnknownHostKeyDecision::Reject;
                        },
                    .hostKeyChanged = {},
                };
                auto client = (*factory)(*request, callbacks, stopToken);
                request->secret.clear();
                if (!client)
                {
                    break;
                }
                *client = withFilenameEncoding(std::move(*client), input->task.filenameEncoding);
                [[maybe_unused]] const auto removed = (*client)->removeEntry(
                    remoteTransferPartialPath(*cleanupDestination, input->task.id), false, stopToken);
            } while (false);
        }
        catch (...)
        {
            // Cleanup is best-effort and must never terminate the process.
            finished->store(true, std::memory_order_release);
            return;
        }
        finished->store(true, std::memory_order_release);
    });
    m_cleanupWorkers.push_back(CleanupWorkerRecord{.finished = finished, .thread = std::move(worker)});
}

void TransferManager::pruneCleanupWorkers()
{
    std::erase_if(m_cleanupWorkers, [](const CleanupWorkerRecord &worker) {
        return worker.finished->load(std::memory_order_acquire);
    });
}

void TransferManager::deliverCredentialError(const QString &taskIdValue, const TransferCredentialError error)
{
    if (m_stopRequested)
    {
        return;
    }
    handleCredentialError(taskId(taskIdValue), error);
}

void TransferManager::deliverHostKeyConfirmation(const QString &taskIdValue, const QString &algorithm,
                                                 const QString &fingerprint)
{
    if (m_stopRequested)
    {
        return;
    }
    emit hostKeyConfirmationRequired(taskIdValue, algorithm, fingerprint);
}

void TransferManager::deliverHostKeyChange(const QString &taskIdValue, const QString &algorithm,
                                           const QString &fingerprint)
{
    if (m_stopRequested)
    {
        return;
    }
    emit hostKeyChanged(taskIdValue, algorithm, fingerprint);
}

void TransferManager::deliverProgress(const QString &taskIdValue, const qulonglong transferredBytes,
                                      const qulonglong totalBytes, const qulonglong bytesPerSecond)
{
    if (m_stopRequested)
    {
        return;
    }
    handleProgress(taskId(taskIdValue), transferredBytes, totalBytes, bytesPerSecond);
}

void TransferManager::deliverResult(const QString &taskIdValue, TransferExecutionResult result)
{
    if (m_stopRequested)
    {
        return;
    }
    handleResult(taskId(taskIdValue), std::move(result));
}

void TransferManager::handleProgress(const std::string &id, const std::uint64_t transferredBytes,
                                     const std::uint64_t totalBytes, const std::uint64_t bytesPerSecond)
{
    if (m_queue.updateProgress(id, transferredBytes, totalBytes, bytesPerSecond))
    {
        publishSnapshot();
    }
}

void TransferManager::handleCredentialError(const std::string &id, const TransferCredentialError error)
{
    m_workers.erase(id);
    if (const auto *task = m_queue.find(id); task != nullptr && task->status == TransferStatus::Running)
    {
        [[maybe_unused]] const auto attention = m_queue.needsAttention(id, credentialErrorCode(error));
        publishSnapshot();
        schedule();
    }
}

void TransferManager::handleResult(const std::string &id, TransferExecutionResult result)
{
    m_workers.erase(id);
    const TransferTask *task = m_queue.find(id);
    if (task == nullptr
        || (task->status != TransferStatus::Running && task->status != TransferStatus::Pausing
            && task->status != TransferStatus::Cancelling))
    {
        schedule();
        return;
    }

    const auto now = QDateTime::currentMSecsSinceEpoch();
    switch (result.kind)
    {
        case TransferExecutionResultKind::Completed:
        case TransferExecutionResultKind::Skipped:
        {
            [[maybe_unused]] const auto updated =
                m_queue.updateProgress(id, result.transferredBytes, task->totalBytes, 0);
            Q_ASSERT(updated.has_value());
            [[maybe_unused]] const auto completed = m_queue.complete(id, now);
            Q_ASSERT(completed.has_value());
            m_work.erase(id);
            break;
        }
        case TransferExecutionResultKind::Cancelled:
        {
            if (task->status == TransferStatus::Pausing)
            {
                [[maybe_unused]] const auto paused = m_queue.markPaused(id, result.transferredBytes);
                Q_ASSERT(paused.has_value());
            }
            else
            {
                [[maybe_unused]] const auto cancelled = m_queue.cancel(id, now);
                Q_ASSERT(cancelled.has_value());
                m_work.erase(id);
            }
            break;
        }
        case TransferExecutionResultKind::Paused:
        {
            [[maybe_unused]] const auto paused = m_queue.markPaused(id, result.transferredBytes);
            Q_ASSERT(paused.has_value());
            break;
        }
        case TransferExecutionResultKind::NeedsAttention:
        {
            [[maybe_unused]] const auto attention = m_queue.needsAttention(id, "file-conflict");
            Q_ASSERT(attention.has_value());
            if (result.conflict)
            {
                emit conflictRequired(qTaskId(id), std::make_shared<const FileConflict>(std::move(*result.conflict)));
            }
            break;
        }
        case TransferExecutionResultKind::Failed:
        {
            if (result.transferredBytes >= task->transferredBytes)
            {
                [[maybe_unused]] const auto updated =
                    m_queue.updateProgress(id, result.transferredBytes, task->totalBytes, 0);
            }
            [[maybe_unused]] const auto failed = m_queue.fail(id, executionErrorCode(result), true, now);
            Q_ASSERT(failed.has_value());
            break;
        }
    }
    publishSnapshot();
    schedule();
}

void TransferManager::publishSnapshot(const bool persist)
{
    if (persist)
    {
        persistRecovery();
    }
    emit tasksChanged(snapshot());
}

void TransferManager::persistRecovery()
{
    if (!m_recoveryStore)
    {
        return;
    }
    if (!m_recoveryStore->save(m_queue.tasks()))
    {
        if (!m_recoveryWriteFailed)
        {
            m_recoveryWriteFailed = true;
            emit recoveryError(QStringLiteral("recovery-save-failed"));
        }
        return;
    }
    m_recoveryWriteFailed = false;
}

QString TransferManager::qTaskId(const std::string_view identifier)
{
    return QString::fromUtf8(identifier.data(), static_cast<qsizetype>(identifier.size()));
}

std::string TransferManager::taskId(const QString &identifier)
{
    return identifier.toUtf8().toStdString();
}

} // namespace ztermy::sftp
