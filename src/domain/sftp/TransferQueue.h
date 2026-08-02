#pragma once

#include "domain/sftp/SftpTypes.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <vector>

namespace ztermy::sftp
{

enum class TransferQueueError : std::uint8_t
{
    InvalidTask,
    DuplicateTask,
    TaskNotFound,
    InvalidTransition,
    InvalidProgress,
};

class TransferQueue final
{
public:
    explicit TransferQueue(std::size_t concurrencyLimit = 2);

    [[nodiscard]] std::size_t concurrencyLimit() const noexcept;
    [[nodiscard]] std::size_t runningCount() const noexcept;
    [[nodiscard]] const std::vector<TransferTask> &tasks() const noexcept;
    [[nodiscard]] const TransferTask *find(std::string_view taskId) const noexcept;

    [[nodiscard]] std::expected<void, TransferQueueError> enqueue(TransferTask task);
    [[nodiscard]] std::expected<void, TransferQueueError> restoreInterrupted(TransferTask task);
    [[nodiscard]] std::optional<TransferTask> takeNext(std::int64_t startedUtcMs);
    [[nodiscard]] std::expected<void, TransferQueueError> updateProgress(std::string_view taskId,
                                                                         std::uint64_t transferredBytes,
                                                                         std::uint64_t totalBytes,
                                                                         std::uint64_t bytesPerSecond);
    [[nodiscard]] std::expected<void, TransferQueueError> complete(std::string_view taskId, std::int64_t finishedUtcMs);
    [[nodiscard]] std::expected<void, TransferQueueError> fail(std::string_view taskId, std::string_view errorCode,
                                                               bool retryable, std::int64_t finishedUtcMs);
    [[nodiscard]] std::expected<void, TransferQueueError> needsAttention(std::string_view taskId,
                                                                         std::string_view reasonCode);
    [[nodiscard]] std::expected<void, TransferQueueError> cancel(std::string_view taskId, std::int64_t finishedUtcMs);
    [[nodiscard]] std::expected<void, TransferQueueError> retry(std::string_view taskId);
    [[nodiscard]] std::expected<void, TransferQueueError> dismiss(std::string_view taskId);
    [[nodiscard]] std::size_t dismissFinished();

private:
    [[nodiscard]] TransferTask *findMutable(std::string_view taskId) noexcept;
    [[nodiscard]] static std::expected<void, TransferQueueError> transition(TransferTask &task, TransferStatus status);

    std::size_t m_concurrencyLimit = 2;
    std::vector<TransferTask> m_tasks;
};

} // namespace ztermy::sftp
