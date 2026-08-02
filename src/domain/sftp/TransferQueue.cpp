#include "domain/sftp/TransferQueue.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace ztermy::sftp
{

TransferQueue::TransferQueue(const std::size_t concurrencyLimit) : m_concurrencyLimit(concurrencyLimit)
{
    if (concurrencyLimit == 0)
    {
        throw std::invalid_argument("transfer queue concurrency must be positive");
    }
}

std::size_t TransferQueue::concurrencyLimit() const noexcept
{
    return m_concurrencyLimit;
}

std::size_t TransferQueue::runningCount() const noexcept
{
    return static_cast<std::size_t>(std::ranges::count_if(m_tasks, [](const TransferTask &task) {
        return task.status == TransferStatus::Running;
    }));
}

const std::vector<TransferTask> &TransferQueue::tasks() const noexcept
{
    return m_tasks;
}

const TransferTask *TransferQueue::find(const std::string_view taskId) const noexcept
{
    const auto found = std::ranges::find(m_tasks, taskId, &TransferTask::id);
    return found == m_tasks.end() ? nullptr : &*found;
}

std::expected<void, TransferQueueError> TransferQueue::enqueue(TransferTask task)
{
    if (!validTransferTask(task) || task.status != TransferStatus::Queued)
    {
        return std::unexpected(TransferQueueError::InvalidTask);
    }
    if (find(task.id) != nullptr)
    {
        return std::unexpected(TransferQueueError::DuplicateTask);
    }
    m_tasks.push_back(std::move(task));
    return {};
}

std::expected<void, TransferQueueError> TransferQueue::restoreInterrupted(TransferTask task)
{
    if (!validTransferTask(task) || task.status != TransferStatus::Failed || !task.retryable
        || task.errorCode != "interrupted")
    {
        return std::unexpected(TransferQueueError::InvalidTask);
    }
    if (find(task.id) != nullptr)
    {
        return std::unexpected(TransferQueueError::DuplicateTask);
    }
    m_tasks.push_back(std::move(task));
    return {};
}

std::optional<TransferTask> TransferQueue::takeNext(const std::int64_t startedUtcMs)
{
    if (runningCount() >= m_concurrencyLimit)
    {
        return std::nullopt;
    }
    const auto queued = std::ranges::find(m_tasks, TransferStatus::Queued, &TransferTask::status);
    if (queued == m_tasks.end())
    {
        return std::nullopt;
    }
    queued->status = TransferStatus::Running;
    queued->startedUtcMs = startedUtcMs;
    queued->finishedUtcMs.reset();
    queued->errorCode.clear();
    return *queued;
}

std::expected<void, TransferQueueError> TransferQueue::updateProgress(const std::string_view taskId,
                                                                      const std::uint64_t transferredBytes,
                                                                      const std::uint64_t totalBytes,
                                                                      const std::uint64_t bytesPerSecond)
{
    TransferTask *task = findMutable(taskId);
    if (task == nullptr)
    {
        return std::unexpected(TransferQueueError::TaskNotFound);
    }
    if (task->status != TransferStatus::Running)
    {
        return std::unexpected(TransferQueueError::InvalidTransition);
    }
    if (totalBytes != 0 && transferredBytes > totalBytes)
    {
        return std::unexpected(TransferQueueError::InvalidProgress);
    }
    if (task->totalBytes != 0 && totalBytes != 0 && task->totalBytes != totalBytes)
    {
        return std::unexpected(TransferQueueError::InvalidProgress);
    }
    if (transferredBytes < task->transferredBytes)
    {
        return std::unexpected(TransferQueueError::InvalidProgress);
    }
    task->totalBytes = totalBytes;
    task->transferredBytes = transferredBytes;
    task->bytesPerSecond = bytesPerSecond;
    return {};
}

std::expected<void, TransferQueueError> TransferQueue::complete(const std::string_view taskId,
                                                                const std::int64_t finishedUtcMs)
{
    TransferTask *task = findMutable(taskId);
    if (task == nullptr)
    {
        return std::unexpected(TransferQueueError::TaskNotFound);
    }
    auto changed = transition(*task, TransferStatus::Completed);
    if (!changed)
    {
        return changed;
    }
    if (task->totalBytes != 0)
    {
        task->transferredBytes = task->totalBytes;
    }
    task->bytesPerSecond = 0;
    task->finishedUtcMs = finishedUtcMs;
    return {};
}

std::expected<void, TransferQueueError> TransferQueue::fail(const std::string_view taskId,
                                                            const std::string_view errorCode, const bool retryable,
                                                            const std::int64_t finishedUtcMs)
{
    TransferTask *task = findMutable(taskId);
    if (task == nullptr)
    {
        return std::unexpected(TransferQueueError::TaskNotFound);
    }
    if (errorCode.empty() || errorCode.find('\0') != std::string_view::npos)
    {
        return std::unexpected(TransferQueueError::InvalidTask);
    }
    auto changed = transition(*task, TransferStatus::Failed);
    if (!changed)
    {
        return changed;
    }
    task->errorCode = errorCode;
    task->retryable = retryable;
    task->bytesPerSecond = 0;
    task->finishedUtcMs = finishedUtcMs;
    return {};
}

std::expected<void, TransferQueueError> TransferQueue::needsAttention(const std::string_view taskId,
                                                                      const std::string_view reasonCode)
{
    TransferTask *task = findMutable(taskId);
    if (task == nullptr)
    {
        return std::unexpected(TransferQueueError::TaskNotFound);
    }
    if (reasonCode.empty() || reasonCode.find('\0') != std::string_view::npos)
    {
        return std::unexpected(TransferQueueError::InvalidTask);
    }
    auto changed = transition(*task, TransferStatus::NeedsAttention);
    if (!changed)
    {
        return changed;
    }
    task->errorCode = reasonCode;
    task->bytesPerSecond = 0;
    return {};
}

std::expected<void, TransferQueueError> TransferQueue::cancel(const std::string_view taskId,
                                                              const std::int64_t finishedUtcMs)
{
    TransferTask *task = findMutable(taskId);
    if (task == nullptr)
    {
        return std::unexpected(TransferQueueError::TaskNotFound);
    }
    auto changed = transition(*task, TransferStatus::Cancelled);
    if (!changed)
    {
        return changed;
    }
    task->bytesPerSecond = 0;
    task->finishedUtcMs = finishedUtcMs;
    return {};
}

std::expected<void, TransferQueueError> TransferQueue::retry(const std::string_view taskId)
{
    TransferTask *task = findMutable(taskId);
    if (task == nullptr)
    {
        return std::unexpected(TransferQueueError::TaskNotFound);
    }
    if (!task->retryable)
    {
        return std::unexpected(TransferQueueError::InvalidTransition);
    }
    auto changed = transition(*task, TransferStatus::Queued);
    if (!changed)
    {
        return changed;
    }
    task->transferredBytes = 0;
    task->bytesPerSecond = 0;
    task->startedUtcMs.reset();
    task->finishedUtcMs.reset();
    task->errorCode.clear();
    return {};
}

TransferTask *TransferQueue::findMutable(const std::string_view taskId) noexcept
{
    const auto found = std::ranges::find(m_tasks, taskId, &TransferTask::id);
    return found == m_tasks.end() ? nullptr : &*found;
}

std::expected<void, TransferQueueError> TransferQueue::transition(TransferTask &task, const TransferStatus status)
{
    if (!canTransition(task.status, status))
    {
        return std::unexpected(TransferQueueError::InvalidTransition);
    }
    task.status = status;
    return {};
}

} // namespace ztermy::sftp
