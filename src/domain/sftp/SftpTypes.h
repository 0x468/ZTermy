#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace ztermy::sftp
{

enum class RemotePathError : std::uint8_t
{
    Empty,
    NotAbsolute,
    ContainsNull,
    TraversesAboveRoot,
    InvalidName,
};

enum class EntryType : std::uint8_t
{
    RegularFile,
    Directory,
    SymbolicLink,
    Other,
};

struct DirectoryEntry final
{
    std::string name;
    std::string remotePath;
    EntryType type = EntryType::Other;
    std::uint64_t size = 0;
    std::optional<std::int64_t> modifiedUtcSeconds;
    std::uint32_t permissions = 0;

    bool operator==(const DirectoryEntry &) const = default;
};

[[nodiscard]] std::expected<std::string, RemotePathError> normalizeRemotePath(std::string_view path);
[[nodiscard]] std::expected<std::string, RemotePathError> joinRemotePath(std::string_view directory,
                                                                         std::string_view name);
[[nodiscard]] std::string parentRemotePath(std::string_view normalizedPath);
[[nodiscard]] bool validRemoteName(std::string_view name) noexcept;
[[nodiscard]] bool hiddenRemoteName(std::string_view name) noexcept;

enum class TransferDirection : std::uint8_t
{
    Upload,
    Download,
};

enum class TransferStatus : std::uint8_t
{
    Queued,
    Running,
    Cancelling,
    NeedsAttention,
    Completed,
    Failed,
    Cancelled,
};

struct TransferTask final
{
    std::string id;
    std::string endpointId;
    std::string displayName;
    std::string sourcePath;
    std::string destinationPath;
    TransferDirection direction = TransferDirection::Download;
    TransferStatus status = TransferStatus::Queued;
    std::uint64_t totalBytes = 0;
    std::uint64_t transferredBytes = 0;
    std::uint64_t bytesPerSecond = 0;
    std::optional<std::int64_t> startedUtcMs;
    std::optional<std::int64_t> finishedUtcMs;
    std::string errorCode;
    bool retryable = true;

    bool operator==(const TransferTask &) const = default;
};

[[nodiscard]] bool validTransferTask(const TransferTask &task) noexcept;
[[nodiscard]] bool canTransition(TransferStatus from, TransferStatus to) noexcept;

enum class ConflictAction : std::uint8_t
{
    Skip,
    Replace,
    Rename,
    Cancel,
};

struct FileConflict final
{
    std::string taskId;
    std::string sourcePath;
    std::string destinationPath;
    EntryType sourceType = EntryType::Other;
    EntryType destinationType = EntryType::Other;
    std::uint64_t sourceSize = 0;
    std::uint64_t destinationSize = 0;
    std::optional<std::int64_t> sourceModifiedUtcSeconds;
    std::optional<std::int64_t> destinationModifiedUtcSeconds;

    bool operator==(const FileConflict &) const = default;
};

[[nodiscard]] bool canReplaceConflict(const FileConflict &conflict) noexcept;

} // namespace ztermy::sftp
