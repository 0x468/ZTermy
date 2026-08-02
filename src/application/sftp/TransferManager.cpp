#include "application/sftp/TransferManager.h"

#include "infrastructure/sftp/TransferRecoveryStore.h"

#include <QDateTime>
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
    for (auto &[id, worker] : m_workers)
    {
        (void)id;
        worker.thread.request_stop();
        worker.context->hostKeyAvailable.notify_all();
    }
    m_workers.clear();
}

std::expected<void, TransferQueueError>
TransferManager::enqueue(TransferTask task, TransferRequestProvider requestProvider, TransferExecutionOptions options)
{
    if (!requestProvider)
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

void TransferManager::retry(const QString &taskIdentifier)
{
    const std::string id = taskId(taskIdentifier);
    if (m_work.contains(id) && m_queue.retry(id))
    {
        publishSnapshot();
        schedule();
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
            auto result = executeTransfer(
                **client, task, spec.options,
                [this, id = task.id](const std::uint64_t transferred, const std::uint64_t total,
                                     const std::uint64_t speed) {
                    QMetaObject::invokeMethod(this, "deliverProgress", Qt::QueuedConnection,
                                              Q_ARG(QString, qTaskId(id)), Q_ARG(qulonglong, transferred),
                                              Q_ARG(qulonglong, total), Q_ARG(qulonglong, speed));
                },
                stopToken);
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

void TransferManager::deliverCredentialError(const QString &taskIdValue, const TransferCredentialError error)
{
    handleCredentialError(taskId(taskIdValue), error);
}

void TransferManager::deliverHostKeyConfirmation(const QString &taskIdValue, const QString &algorithm,
                                                 const QString &fingerprint)
{
    emit hostKeyConfirmationRequired(taskIdValue, algorithm, fingerprint);
}

void TransferManager::deliverHostKeyChange(const QString &taskIdValue, const QString &algorithm,
                                           const QString &fingerprint)
{
    emit hostKeyChanged(taskIdValue, algorithm, fingerprint);
}

void TransferManager::deliverProgress(const QString &taskIdValue, const qulonglong transferredBytes,
                                      const qulonglong totalBytes, const qulonglong bytesPerSecond)
{
    handleProgress(taskId(taskIdValue), transferredBytes, totalBytes, bytesPerSecond);
}

void TransferManager::deliverResult(const QString &taskIdValue, TransferExecutionResult result)
{
    handleResult(taskId(taskIdValue), std::move(result));
}

void TransferManager::handleProgress(const std::string &id, const std::uint64_t transferredBytes,
                                     const std::uint64_t totalBytes, const std::uint64_t bytesPerSecond)
{
    if (m_queue.updateProgress(id, transferredBytes, totalBytes, bytesPerSecond))
    {
        publishSnapshot(false);
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
    if (task == nullptr || task->status != TransferStatus::Running)
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
            const auto updated = m_queue.updateProgress(id, result.transferredBytes, task->totalBytes, 0);
            Q_ASSERT(updated.has_value());
            const auto completed = m_queue.complete(id, now);
            Q_ASSERT(completed.has_value());
            m_work.erase(id);
            break;
        }
        case TransferExecutionResultKind::Cancelled:
        {
            const auto cancelled = m_queue.cancel(id, now);
            Q_ASSERT(cancelled.has_value());
            m_work.erase(id);
            break;
        }
        case TransferExecutionResultKind::NeedsAttention:
        {
            const auto attention = m_queue.needsAttention(id, "file-conflict");
            Q_ASSERT(attention.has_value());
            if (result.conflict)
            {
                emit conflictRequired(qTaskId(id), std::make_shared<const FileConflict>(std::move(*result.conflict)));
            }
            break;
        }
        case TransferExecutionResultKind::Failed:
        {
            const auto failed = m_queue.fail(id, executionErrorCode(result), true, now);
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
