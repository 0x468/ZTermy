#include "application/sftp/TransferBatchCoordinator.h"

#include "application/sftp/SftpFilenameCodec.h"
#include "application/sftp/TransferBatchMaterializer.h"
#include "application/sftp/TransferSourceAdapters.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace ztermy::sftp
{
namespace
{

[[nodiscard]] TransferPlanEntryStatus entryStatus(const TransferStatus status) noexcept
{
    switch (status)
    {
        case TransferStatus::Queued:
            return TransferPlanEntryStatus::Queued;
        case TransferStatus::Running:
        case TransferStatus::Pausing:
            return TransferPlanEntryStatus::Running;
        case TransferStatus::Paused:
            return TransferPlanEntryStatus::Paused;
        case TransferStatus::Cancelling:
            return TransferPlanEntryStatus::Running;
        case TransferStatus::NeedsAttention:
            return TransferPlanEntryStatus::NeedsAttention;
        case TransferStatus::Completed:
            return TransferPlanEntryStatus::Completed;
        case TransferStatus::Failed:
            return TransferPlanEntryStatus::Failed;
        case TransferStatus::Cancelled:
            return TransferPlanEntryStatus::Cancelled;
    }
    return TransferPlanEntryStatus::Failed;
}

void updateBatchStatus(TransferBatch &batch)
{
    bool queuedOrRunning = false;
    bool paused = false;
    bool needsAttention = false;
    bool failed = false;
    bool cancelled = false;
    bool incomplete = false;
    for (const TransferPlanEntry &entry : batch.entries)
    {
        if (entry.kind != TransferPlanEntryKind::RegularFile)
        {
            continue;
        }
        queuedOrRunning = queuedOrRunning || entry.status == TransferPlanEntryStatus::Queued
                          || entry.status == TransferPlanEntryStatus::Running;
        paused = paused || entry.status == TransferPlanEntryStatus::Paused;
        needsAttention = needsAttention || entry.status == TransferPlanEntryStatus::NeedsAttention;
        failed = failed || entry.status == TransferPlanEntryStatus::Failed;
        cancelled = cancelled || entry.status == TransferPlanEntryStatus::Cancelled;
        incomplete =
            incomplete
            || (entry.status != TransferPlanEntryStatus::Completed && entry.status != TransferPlanEntryStatus::Skipped);
    }
    if (needsAttention)
    {
        batch.status = TransferBatchStatus::NeedsAttention;
    }
    else if (queuedOrRunning)
    {
        batch.status = TransferBatchStatus::Running;
    }
    else if (paused)
    {
        batch.status = TransferBatchStatus::Paused;
    }
    else if (failed)
    {
        batch.status = TransferBatchStatus::Failed;
    }
    else if (cancelled && incomplete)
    {
        batch.status = TransferBatchStatus::Cancelled;
    }
    else
    {
        batch.status = TransferBatchStatus::Completed;
    }
}

} // namespace

TransferBatchCoordinator::TransferBatchCoordinator(TransferManager &transferManager, SftpClientFactory clientFactory,
                                                   QObject *parent)
    : QObject(parent),
      m_transferManager(transferManager),
      m_clientFactory(clientFactory ? std::move(clientFactory) : createSftpClient)
{
    qRegisterMetaType<TransferBatchesPtr>();
    qRegisterMetaType<TransferBatchPlanningOutcomePtr>();
    connect(&m_transferManager, &TransferManager::tasksChanged, this, &TransferBatchCoordinator::applyTaskSnapshot);
    connect(this, &TransferBatchCoordinator::planningCompleted, this, &TransferBatchCoordinator::completePlanning,
            Qt::QueuedConnection);
}

TransferBatchCoordinator::~TransferBatchCoordinator()
{
    requestStop();
    m_planners.clear();
}

std::expected<std::string, TransferBatchCoordinatorError>
TransferBatchCoordinator::enqueue(TransferPlanRequest request, TransferRequestProvider requestProvider,
                                  std::string filenameEncoding)
{
    TransferBatch placeholder{.id = request.batchId,
                              .endpointId = request.endpointId,
                              .displayName = request.displayName,
                              .destinationRoot = request.destinationRoot,
                              .sourceRoots = request.sourceRoots,
                              .direction = request.direction,
                              .conflictPolicy = request.conflictPolicy};
    if (m_stopRequested || !requestProvider || filenameEncoding.empty() || !validTransferBatch(placeholder))
    {
        return std::unexpected(TransferBatchCoordinatorError::InvalidRequest);
    }
    if (m_batches.size() >= maximumTransferBatches || m_planners.contains(request.batchId)
        || findBatch(request.batchId) != nullptr)
    {
        return std::unexpected(TransferBatchCoordinatorError::Capacity);
    }

    std::string batchId = request.batchId;
    m_batches.push_back(std::move(placeholder));
    publishSnapshot();
    std::jthread planner([this, request = std::move(request), requestProvider = std::move(requestProvider),
                          filenameEncoding = std::move(filenameEncoding)](const std::stop_token &stopToken) mutable {
        auto outcome = std::make_shared<TransferBatchPlanningOutcome>();
        outcome->batchId = request.batchId;
        outcome->requestProvider = requestProvider;
        try
        {
            std::unique_ptr<SftpClient> client;
            if (request.direction == TransferBatchDirection::Download)
            {
                auto connection = requestProvider();
                if (!connection)
                {
                    outcome->errorCode = "credentials-unavailable";
                }
                else
                {
                    const ssh::SshConnectionCallbacks callbacks{
                        .phaseChanged = {},
                        .confirmUnknownHostKey =
                            [](const QString &, const QString &, const QString &) {
                                return ssh::UnknownHostKeyDecision::Reject;
                            },
                        .hostKeyChanged = {},
                    };
                    auto created = m_clientFactory(*connection, callbacks, stopToken);
                    connection->secret.clear();
                    if (created)
                    {
                        client = withFilenameEncoding(std::move(*created), filenameEncoding);
                        RemoteTransferSourceTree source(*client);
                        auto planned = planTransferTree(request, source, stopToken);
                        if (planned)
                        {
                            outcome->batch = std::move(*planned);
                        }
                    }
                    if (!client || !outcome->batch)
                    {
                        outcome->errorCode = stopToken.stop_requested() ? "cancelled" : "remote-planning";
                    }
                }
            }
            else
            {
                LocalTransferSourceTree source;
                auto planned = planTransferTree(request, source, stopToken);
                if (planned)
                {
                    outcome->batch = std::move(*planned);
                }
                else
                {
                    outcome->errorCode = stopToken.stop_requested() ? "cancelled" : "local-planning";
                }
            }

            if (outcome->batch && request.direction == TransferBatchDirection::Upload)
            {
                auto connection = requestProvider();
                if (!connection)
                {
                    outcome->errorCode = "credentials-unavailable";
                    outcome->batch.reset();
                }
                else
                {
                    const ssh::SshConnectionCallbacks callbacks{
                        .phaseChanged = {},
                        .confirmUnknownHostKey =
                            [](const QString &, const QString &, const QString &) {
                                return ssh::UnknownHostKeyDecision::Reject;
                            },
                        .hostKeyChanged = {},
                    };
                    auto created = m_clientFactory(*connection, callbacks, stopToken);
                    connection->secret.clear();
                    if (created)
                    {
                        client = withFilenameEncoding(std::move(*created), filenameEncoding);
                    }
                    else
                    {
                        outcome->errorCode = "remote-materialization";
                        outcome->batch.reset();
                    }
                }
            }
            if (outcome->batch)
            {
                auto tasks = materializeTransferBatch(*outcome->batch, client.get(), filenameEncoding, stopToken);
                if (tasks)
                {
                    outcome->tasks = std::move(*tasks);
                }
                else
                {
                    outcome->errorCode = stopToken.stop_requested() ? "cancelled" : "materialization";
                }
            }
        }
        catch (...)
        {
            outcome->batch.reset();
            outcome->tasks.clear();
            outcome->errorCode = "planning-exception";
        }
        emit planningCompleted(std::move(outcome));
    });
    m_planners.emplace(batchId, std::move(planner));
    return batchId;
}

TransferBatchesPtr TransferBatchCoordinator::snapshot() const
{
    return std::make_shared<const std::vector<TransferBatch>>(m_batches);
}

void TransferBatchCoordinator::requestStop() noexcept
{
    m_stopRequested = true;
    for (auto &[id, planner] : m_planners)
    {
        (void)id;
        planner.request_stop();
    }
}

void TransferBatchCoordinator::pause(const QString &batchId)
{
    if (const TransferBatch *batch = findBatch(batchId.toStdString()))
    {
        forEachChild(*batch, [this](const QString &taskId) {
            m_transferManager.pause(taskId);
        });
    }
}

void TransferBatchCoordinator::resume(const QString &batchId)
{
    if (const TransferBatch *batch = findBatch(batchId.toStdString()))
    {
        forEachChild(*batch, [this](const QString &taskId) {
            m_transferManager.resume(taskId);
        });
    }
}

void TransferBatchCoordinator::cancel(const QString &batchId)
{
    const std::string id = batchId.toStdString();
    if (auto planner = m_planners.find(id); planner != m_planners.end())
    {
        planner->second.request_stop();
    }
    if (TransferBatch *batch = findBatch(id))
    {
        batch->status = TransferBatchStatus::Cancelled;
        forEachChild(*batch, [this](const QString &taskId) {
            m_transferManager.cancel(taskId);
        });
        publishSnapshot();
    }
}

void TransferBatchCoordinator::retry(const QString &batchId)
{
    if (const TransferBatch *batch = findBatch(batchId.toStdString()))
    {
        forEachChild(*batch, [this](const QString &taskId) {
            m_transferManager.retry(taskId);
        });
    }
}

void TransferBatchCoordinator::dismiss(const QString &batchId)
{
    const std::string id = batchId.toStdString();
    const auto found = std::ranges::find(m_batches, id, &TransferBatch::id);
    if (found == m_batches.end() || m_planners.contains(id)
        || (found->status != TransferBatchStatus::Completed && found->status != TransferBatchStatus::Failed
            && found->status != TransferBatchStatus::Cancelled))
    {
        return;
    }
    forEachChild(*found, [this](const QString &taskId) {
        m_transferManager.dismiss(taskId);
    });
    m_batches.erase(found);
    publishSnapshot();
}

void TransferBatchCoordinator::completePlanning(const TransferBatchPlanningOutcomePtr &outcome)
{
    m_planners.erase(outcome->batchId);
    TransferBatch *placeholder = findBatch(outcome->batchId);
    if (placeholder == nullptr || placeholder->status == TransferBatchStatus::Cancelled)
    {
        return;
    }
    if (!outcome->batch || !outcome->errorCode.empty())
    {
        placeholder->status =
            outcome->errorCode == "cancelled" ? TransferBatchStatus::Cancelled : TransferBatchStatus::Failed;
        placeholder->discoveryErrorCode = outcome->errorCode;
        publishSnapshot();
        return;
    }
    *placeholder = std::move(*outcome->batch);
    for (TransferTask &task : outcome->tasks)
    {
        const std::string childId = task.id;
        if (auto queued = m_transferManager.enqueue(std::move(task), outcome->requestProvider, {}); !queued)
        {
            const auto entry = std::ranges::find(placeholder->entries, childId, &TransferPlanEntry::childTaskId);
            if (entry != placeholder->entries.end())
            {
                entry->status = TransferPlanEntryStatus::Failed;
                entry->errorCode = "queue-rejected";
            }
            placeholder->status = TransferBatchStatus::Failed;
        }
    }
    publishSnapshot();
}

void TransferBatchCoordinator::applyTaskSnapshot(const TransferTasksPtr &tasks)
{
    bool changed = false;
    for (TransferBatch &batch : m_batches)
    {
        if (batch.status == TransferBatchStatus::Discovering || batch.entries.empty())
        {
            continue;
        }
        for (TransferPlanEntry &entry : batch.entries)
        {
            if (entry.childTaskId.empty())
            {
                continue;
            }
            const auto task = std::ranges::find(*tasks, entry.childTaskId, &TransferTask::id);
            if (task == tasks->end())
            {
                continue;
            }
            const TransferPlanEntryStatus status = entryStatus(task->status);
            const std::string errorCode = status == TransferPlanEntryStatus::Failed ? task->errorCode : std::string{};
            if (entry.status != status || entry.transferredBytes != task->transferredBytes
                || entry.errorCode != errorCode)
            {
                entry.status = status;
                entry.transferredBytes = task->transferredBytes;
                entry.errorCode = errorCode;
                changed = true;
            }
        }
        const TransferBatchStatus previous = batch.status;
        updateBatchStatus(batch);
        changed = changed || previous != batch.status;
    }
    if (changed)
    {
        publishSnapshot();
    }
}

void TransferBatchCoordinator::publishSnapshot()
{
    emit batchesChanged(snapshot());
}

TransferBatch *TransferBatchCoordinator::findBatch(const std::string_view batchId)
{
    const auto found = std::ranges::find(m_batches, batchId, &TransferBatch::id);
    return found == m_batches.end() ? nullptr : &*found;
}

const TransferBatch *TransferBatchCoordinator::findBatch(const std::string_view batchId) const
{
    const auto found = std::ranges::find(m_batches, batchId, &TransferBatch::id);
    return found == m_batches.end() ? nullptr : &*found;
}

void TransferBatchCoordinator::forEachChild(const TransferBatch &batch,
                                            const std::function<void(const QString &)> &operation)
{
    for (const TransferPlanEntry &entry : batch.entries)
    {
        if (!entry.childTaskId.empty())
        {
            operation(QString::fromStdString(entry.childTaskId));
        }
    }
}

} // namespace ztermy::sftp
