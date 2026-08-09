#pragma once

#include "application/sftp/SftpClient.h"
#include "domain/sftp/SftpTypes.h"

#include <QString>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

namespace ztermy::sftp
{

enum class TransferExecutionResultKind : std::uint8_t
{
    Completed,
    Skipped,
    Paused,
    Cancelled,
    NeedsAttention,
    Failed,
};

enum class TransferExecutionErrorKind : std::uint8_t
{
    InvalidTask,
    LocalIo,
    RemoteIo,
    IncompatibleConflict,
    InvalidConflictRename,
    CommitFailed,
};

struct TransferExecutionOptions final
{
    std::optional<ConflictAction> conflictAction;
    std::string renamedDestinationPath;
};

struct TransferExecutionResult final
{
    TransferExecutionResultKind kind = TransferExecutionResultKind::Failed;
    std::uint64_t transferredBytes = 0;
    std::string finalDestinationPath;
    std::optional<FileConflict> conflict;
    std::optional<TransferExecutionErrorKind> error;
    std::optional<ssh::SshTransportError> transportError;
};

using TransferProgressCallback =
    std::function<void(std::uint64_t transferredBytes, std::uint64_t totalBytes, std::uint64_t bytesPerSecond)>;
using TransferPauseRequestedCallback = std::function<bool()>;

[[nodiscard]] QString localTransferPartialPath(std::string_view destinationPath, std::string_view taskId);
[[nodiscard]] std::string remoteTransferPartialPath(std::string_view destinationPath, std::string_view taskId);

[[nodiscard]] TransferExecutionResult executeTransfer(SftpClient &client, const TransferTask &task,
                                                      const TransferExecutionOptions &options,
                                                      const TransferProgressCallback &progress,
                                                      const std::stop_token &stopToken,
                                                      const TransferPauseRequestedCallback &pauseRequested = {});

} // namespace ztermy::sftp
