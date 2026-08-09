#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::sftp
{

enum class TransferBatchDirection : std::uint8_t
{
    Upload,
    Download,
};

enum class TransferPlanEntryKind : std::uint8_t
{
    Directory,
    RegularFile,
    SymbolicLink,
    Unsupported,
};

enum class TransferPlanEntryStatus : std::uint8_t
{
    Pending,
    Queued,
    Running,
    Completed,
    Skipped,
    Failed,
    Cancelled,
    Interrupted,
};

enum class TransferBatchStatus : std::uint8_t
{
    Discovering,
    Ready,
    Running,
    Paused,
    NeedsAttention,
    Completed,
    Failed,
    Cancelled,
    Interrupted,
};

enum class TransferConflictPolicy : std::uint8_t
{
    Ask,
    Replace,
    Rename,
    Skip,
};

struct TransferPlanEntry final
{
    std::string id;
    std::string parentId;
    std::string relativePath;
    std::string sourcePath;
    std::string childTaskId;
    std::string errorCode;
    TransferPlanEntryKind kind = TransferPlanEntryKind::RegularFile;
    TransferPlanEntryStatus status = TransferPlanEntryStatus::Pending;
    std::uint64_t totalBytes = 0;
    std::uint64_t transferredBytes = 0;
    std::uint32_t depth = 0;
    std::optional<std::int64_t> sourceModifiedUtcSeconds;

    [[nodiscard]] friend bool operator==(const TransferPlanEntry &, const TransferPlanEntry &) = default;
};

struct TransferBatch final
{
    std::string id;
    std::string endpointId;
    std::string displayName;
    std::string destinationRoot;
    std::string discoveryErrorCode;
    std::vector<std::string> sourceRoots;
    std::vector<TransferPlanEntry> entries;
    TransferBatchDirection direction = TransferBatchDirection::Download;
    TransferBatchStatus status = TransferBatchStatus::Discovering;
    TransferConflictPolicy conflictPolicy = TransferConflictPolicy::Ask;
    bool applyConflictPolicyToRemaining = false;

    [[nodiscard]] friend bool operator==(const TransferBatch &, const TransferBatch &) = default;
};

struct TransferBatchSummary final
{
    std::size_t entryCount = 0;
    std::size_t directoryCount = 0;
    std::size_t regularFileCount = 0;
    std::size_t completedCount = 0;
    std::size_t skippedCount = 0;
    std::size_t failedCount = 0;
    std::size_t activeCount = 0;
    std::uint64_t totalBytes = 0;
    std::uint64_t transferredBytes = 0;
    bool discoveryComplete = false;
    bool terminal = false;

    [[nodiscard]] friend bool operator==(const TransferBatchSummary &, const TransferBatchSummary &) = default;
};

enum class TransferBatchError : std::uint8_t
{
    InvalidBatch,
    InvalidEntry,
    DuplicateEntryId,
    DuplicateRelativePath,
    MissingParent,
    ParentNotDirectory,
    DepthMismatch,
    EntryLimit,
    InvalidTransition,
};

inline constexpr std::size_t maximumTransferBatches = 32;
inline constexpr std::size_t maximumTransferSourceRoots = 256;
inline constexpr std::size_t maximumTransferPlanEntries = 100'000;
inline constexpr std::uint32_t maximumTransferTreeDepth = 64;
inline constexpr std::size_t maximumTransferPathUtf8Bytes = 4'096;

[[nodiscard]] bool validTransferRelativePath(std::string_view path) noexcept;
[[nodiscard]] bool validTransferPlanEntry(const TransferPlanEntry &entry) noexcept;
[[nodiscard]] bool validTransferBatch(const TransferBatch &batch);
[[nodiscard]] std::expected<void, TransferBatchError> appendTransferPlanEntry(TransferBatch &batch,
                                                                              TransferPlanEntry entry);
[[nodiscard]] std::expected<void, TransferBatchError>
updateTransferPlanEntry(TransferBatch &batch, std::string_view entryId, TransferPlanEntryStatus status,
                        std::uint64_t transferredBytes = 0, std::string errorCode = {});
[[nodiscard]] std::expected<void, TransferBatchError> finalizeTransferDiscovery(TransferBatch &batch,
                                                                                std::string errorCode = {});
[[nodiscard]] TransferBatchSummary summarizeTransferBatch(const TransferBatch &batch) noexcept;

} // namespace ztermy::sftp
