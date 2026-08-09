#include "application/sftp/TransferExecutor.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

#include <array>
#include <chrono>
#include <functional>
#include <string_view>
#include <utility>

namespace ztermy::sftp
{
namespace
{

constexpr std::size_t transferBufferSize = std::size_t{256} * 1024;
constexpr auto progressInterval = std::chrono::milliseconds(50);

QString localPath(const std::string_view path)
{
    return QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size()));
}

EntryType localEntryType(const QFileInfo &info)
{
    if (info.isSymbolicLink())
    {
        return EntryType::SymbolicLink;
    }
    if (info.isDir())
    {
        return EntryType::Directory;
    }
    if (info.isFile())
    {
        return EntryType::RegularFile;
    }
    return EntryType::Other;
}

std::string temporaryRemotePath(const std::string_view destination, const std::string_view taskId)
{
    const QByteArray id(taskId.data(), static_cast<qsizetype>(taskId.size()));
    const QByteArray token = QCryptographicHash::hash(id, QCryptographicHash::Sha256).toHex().first(16);
    return std::string(destination) + ".ztermy-part-" + token.toStdString();
}

QString temporaryLocalPath(const std::string_view destination, const std::string_view taskId)
{
    const QByteArray id(taskId.data(), static_cast<qsizetype>(taskId.size()));
    const QByteArray token = QCryptographicHash::hash(id, QCryptographicHash::Sha256).toHex().first(16);
    return localPath(destination) + QStringLiteral(".ztermy-part-") + QString::fromLatin1(token);
}

std::uint64_t speedSince(const std::chrono::steady_clock::time_point started, const std::uint64_t bytes)
{
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    if (milliseconds <= 0)
    {
        return 0;
    }
    return static_cast<std::uint64_t>((bytes * 1000) / static_cast<std::uint64_t>(milliseconds));
}

TransferExecutionResult failure(const TransferExecutionErrorKind kind, const std::uint64_t transferred = 0)
{
    return TransferExecutionResult{
        .kind = TransferExecutionResultKind::Failed,
        .transferredBytes = transferred,
        .error = kind,
    };
}

TransferExecutionResult remoteFailure(const ssh::SshTransportError error, const std::uint64_t transferred = 0)
{
    return TransferExecutionResult{
        .kind = error.kind == ssh::SshTransportErrorKind::Cancelled ? TransferExecutionResultKind::Cancelled
                                                                    : TransferExecutionResultKind::Failed,
        .transferredBytes = transferred,
        .error = TransferExecutionErrorKind::RemoteIo,
        .transportError = error,
    };
}

TransferExecutionResult conflictResult(FileConflict conflict)
{
    return TransferExecutionResult{
        .kind = TransferExecutionResultKind::NeedsAttention,
        .conflict = std::move(conflict),
    };
}

bool commitLocalFile(const QString &partialPath, const QString &finalPath)
{
#ifdef Q_OS_WIN
    const QString nativePartial = QDir::toNativeSeparators(partialPath);
    const QString nativeFinal = QDir::toNativeSeparators(finalPath);
    return MoveFileExW(reinterpret_cast<LPCWSTR>(nativePartial.utf16()), reinterpret_cast<LPCWSTR>(nativeFinal.utf16()),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
           != FALSE;
#else
    if (QFileInfo::exists(finalPath) && !QFile::remove(finalPath))
    {
        return false;
    }
    return QFile::rename(partialPath, finalPath);
#endif
}

class RemoteFileGuard final
{
public:
    explicit RemoteFileGuard(SftpClient &client) noexcept : m_client(client) {}
    ~RemoteFileGuard()
    {
        if (m_active)
        {
            [[maybe_unused]] const auto closed = m_client.closeFile({});
        }
    }

    [[nodiscard]] std::expected<void, ssh::SshTransportError> close(const std::stop_token &stopToken)
    {
        if (!m_active)
        {
            return {};
        }
        auto result = m_client.closeFile(stopToken);
        if (result)
        {
            m_active = false;
        }
        return result;
    }

private:
    SftpClient &m_client;
    bool m_active = true;
};

TransferExecutionResult executeDownload(SftpClient &client, const TransferTask &task,
                                        const TransferExecutionOptions &options,
                                        const TransferProgressCallback &progress, const std::stop_token &stopToken,
                                        const TransferPauseRequestedCallback &pauseRequested)
{
    auto remoteSource = client.statEntry(task.sourcePath, stopToken);
    if (!remoteSource)
    {
        return remoteFailure(remoteSource.error());
    }
    if (!*remoteSource || (*remoteSource)->type != EntryType::RegularFile)
    {
        return failure(TransferExecutionErrorKind::InvalidTask);
    }
    if (task.sourceModifiedUtcSeconds && (*remoteSource)->modifiedUtcSeconds
        && task.sourceModifiedUtcSeconds != (*remoteSource)->modifiedUtcSeconds)
    {
        return failure(TransferExecutionErrorKind::InvalidTask, task.transferredBytes);
    }

    std::string destination = task.destinationPath;
    QFileInfo destinationInfo(localPath(destination));
    if (destinationInfo.exists())
    {
        FileConflict conflict{
            .taskId = task.id,
            .sourcePath = task.sourcePath,
            .destinationPath = destination,
            .sourceType = (*remoteSource)->type,
            .destinationType = localEntryType(destinationInfo),
            .sourceSize = (*remoteSource)->size,
            .destinationSize = static_cast<std::uint64_t>(destinationInfo.size()),
            .sourceModifiedUtcSeconds = (*remoteSource)->modifiedUtcSeconds,
            .destinationModifiedUtcSeconds = destinationInfo.lastModified().toSecsSinceEpoch(),
        };
        if (!options.conflictAction)
        {
            return conflictResult(std::move(conflict));
        }
        if (*options.conflictAction == ConflictAction::Skip)
        {
            return TransferExecutionResult{.kind = TransferExecutionResultKind::Skipped,
                                           .finalDestinationPath = destination};
        }
        if (*options.conflictAction == ConflictAction::Cancel)
        {
            return TransferExecutionResult{.kind = TransferExecutionResultKind::Cancelled,
                                           .finalDestinationPath = destination};
        }
        if (*options.conflictAction == ConflictAction::Replace && !canReplaceConflict(conflict))
        {
            return failure(TransferExecutionErrorKind::IncompatibleConflict);
        }
        if (*options.conflictAction == ConflictAction::Rename)
        {
            if (options.renamedDestinationPath.empty() || QFileInfo::exists(localPath(options.renamedDestinationPath)))
            {
                return failure(TransferExecutionErrorKind::InvalidConflictRename);
            }
            destination = options.renamedDestinationPath;
        }
    }

    const QString partialPath = temporaryLocalPath(destination, task.id);
    const QFileInfo partialInfo(partialPath);
    std::uint64_t transferred = task.transferredBytes;
    if (task.totalBytes != 0 && task.totalBytes != (*remoteSource)->size)
    {
        return failure(TransferExecutionErrorKind::InvalidTask, transferred);
    }
    if (transferred > 0)
    {
        if (!partialInfo.exists())
        {
            transferred = 0;
        }
        else if (!partialInfo.isFile() || partialInfo.size() < 0
                 || std::cmp_greater(static_cast<std::uint64_t>(partialInfo.size()), (*remoteSource)->size))
        {
            return failure(TransferExecutionErrorKind::InvalidTask, transferred);
        }
        else
        {
            transferred = static_cast<std::uint64_t>(partialInfo.size());
        }
    }
    if (transferred == 0 && partialInfo.exists() && !QFile::remove(partialPath))
    {
        return failure(TransferExecutionErrorKind::LocalIo);
    }

    auto opened = client.openFileForRead(task.sourcePath, stopToken);
    if (!opened)
    {
        return remoteFailure(opened.error());
    }
    RemoteFileGuard remoteGuard(client);
    if (transferred > 0)
    {
        auto sought = client.seekFile(transferred);
        if (!sought)
        {
            return remoteFailure(sought.error(), transferred);
        }
    }

    QFile output(partialPath);
    if (!output.open(QIODevice::WriteOnly | (transferred > 0 ? QIODevice::Append : QIODevice::Truncate)))
    {
        return failure(TransferExecutionErrorKind::LocalIo);
    }

    std::array<char, transferBufferSize> buffer{};
    const auto started = std::chrono::steady_clock::now();
    const std::uint64_t startedAt = transferred;
    auto lastProgress = started;
    while (!stopToken.stop_requested())
    {
        auto read = client.readFile(buffer, stopToken);
        if (!read)
        {
            output.close();
            return remoteFailure(read.error(), transferred);
        }
        if (*read == 0)
        {
            break;
        }
        if (output.write(buffer.data(), static_cast<qint64>(*read)) != static_cast<qint64>(*read))
        {
            output.close();
            return failure(TransferExecutionErrorKind::LocalIo, transferred);
        }
        transferred += *read;
        const auto now = std::chrono::steady_clock::now();
        if (progress && (transferred == (*remoteSource)->size || now - lastProgress >= progressInterval))
        {
            progress(transferred, (*remoteSource)->size, speedSince(started, transferred - startedAt));
            lastProgress = now;
        }
    }
    if (stopToken.stop_requested())
    {
        output.close();
        const bool paused = pauseRequested && pauseRequested();
        if (!paused)
        {
            QFile::remove(partialPath);
        }
        return TransferExecutionResult{.kind = paused ? TransferExecutionResultKind::Paused
                                                      : TransferExecutionResultKind::Cancelled,
                                       .transferredBytes = transferred,
                                       .finalDestinationPath = destination};
    }

    auto closed = remoteGuard.close(stopToken);
    if (!closed)
    {
        output.close();
        return remoteFailure(closed.error(), transferred);
    }
    output.close();
    const QString finalPath = localPath(destination);
    if (QFileInfo::exists(finalPath) && destination != task.destinationPath)
    {
        QFile::remove(partialPath);
        return failure(TransferExecutionErrorKind::InvalidConflictRename, transferred);
    }
    if (!commitLocalFile(partialPath, finalPath))
    {
        return failure(TransferExecutionErrorKind::CommitFailed, transferred);
    }
    return TransferExecutionResult{.kind = TransferExecutionResultKind::Completed,
                                   .transferredBytes = transferred,
                                   .finalDestinationPath = destination};
}

TransferExecutionResult executeUpload(SftpClient &client, const TransferTask &task,
                                      const TransferExecutionOptions &options, const TransferProgressCallback &progress,
                                      const std::stop_token &stopToken,
                                      const TransferPauseRequestedCallback &pauseRequested)
{
    QFileInfo sourceInfo(localPath(task.sourcePath));
    if (!sourceInfo.exists() || !sourceInfo.isFile() || sourceInfo.isSymbolicLink())
    {
        return failure(TransferExecutionErrorKind::InvalidTask);
    }
    if (task.sourceModifiedUtcSeconds && *task.sourceModifiedUtcSeconds != sourceInfo.lastModified().toSecsSinceEpoch())
    {
        return failure(TransferExecutionErrorKind::InvalidTask, task.transferredBytes);
    }
    QFile input(sourceInfo.absoluteFilePath());
    if (!input.open(QIODevice::ReadOnly))
    {
        return failure(TransferExecutionErrorKind::LocalIo);
    }

    std::string destination = task.destinationPath;
    auto remoteDestination = client.statEntry(destination, stopToken);
    if (!remoteDestination)
    {
        return remoteFailure(remoteDestination.error());
    }
    bool replace = false;
    if (*remoteDestination)
    {
        FileConflict conflict{
            .taskId = task.id,
            .sourcePath = task.sourcePath,
            .destinationPath = destination,
            .sourceType = EntryType::RegularFile,
            .destinationType = (*remoteDestination)->type,
            .sourceSize = static_cast<std::uint64_t>(sourceInfo.size()),
            .destinationSize = (*remoteDestination)->size,
            .sourceModifiedUtcSeconds = sourceInfo.lastModified().toSecsSinceEpoch(),
            .destinationModifiedUtcSeconds = (*remoteDestination)->modifiedUtcSeconds,
        };
        if (!options.conflictAction)
        {
            return conflictResult(std::move(conflict));
        }
        if (*options.conflictAction == ConflictAction::Skip)
        {
            return TransferExecutionResult{.kind = TransferExecutionResultKind::Skipped,
                                           .finalDestinationPath = destination};
        }
        if (*options.conflictAction == ConflictAction::Cancel)
        {
            return TransferExecutionResult{.kind = TransferExecutionResultKind::Cancelled,
                                           .finalDestinationPath = destination};
        }
        if (*options.conflictAction == ConflictAction::Replace)
        {
            if (!canReplaceConflict(conflict))
            {
                return failure(TransferExecutionErrorKind::IncompatibleConflict);
            }
            replace = true;
        }
        if (*options.conflictAction == ConflictAction::Rename)
        {
            auto renamed = normalizeRemotePath(options.renamedDestinationPath);
            if (!renamed || *renamed == destination)
            {
                return failure(TransferExecutionErrorKind::InvalidConflictRename);
            }
            auto renamedEntry = client.statEntry(*renamed, stopToken);
            if (!renamedEntry)
            {
                return remoteFailure(renamedEntry.error());
            }
            if (*renamedEntry)
            {
                return failure(TransferExecutionErrorKind::InvalidConflictRename);
            }
            destination = std::move(*renamed);
        }
    }

    const std::string temporaryPath = temporaryRemotePath(destination, task.id);
    auto staleTemporary = client.statEntry(temporaryPath, stopToken);
    if (!staleTemporary)
    {
        return remoteFailure(staleTemporary.error());
    }
    std::uint64_t transferred = task.transferredBytes;
    const auto sourceSize = static_cast<std::uint64_t>(sourceInfo.size());
    if (task.totalBytes != 0 && task.totalBytes != sourceSize)
    {
        return failure(TransferExecutionErrorKind::InvalidTask, transferred);
    }
    if (transferred > 0)
    {
        if (!*staleTemporary)
        {
            transferred = 0;
        }
        else if ((*staleTemporary)->type != EntryType::RegularFile || (*staleTemporary)->size > sourceSize)
        {
            return failure(TransferExecutionErrorKind::InvalidTask, transferred);
        }
        else
        {
            transferred = (*staleTemporary)->size;
        }
    }
    if (transferred == 0 && *staleTemporary)
    {
        auto removed = client.removeEntry(temporaryPath, false, stopToken);
        if (!removed)
        {
            return remoteFailure(removed.error());
        }
    }
    auto opened = transferred > 0 ? client.openFileForResume(temporaryPath, stopToken)
                                  : client.openFileForWrite(temporaryPath, false, stopToken);
    if (!opened)
    {
        return remoteFailure(opened.error());
    }
    RemoteFileGuard remoteGuard(client);
    if (transferred > 0)
    {
        auto remoteSeek = client.seekFile(transferred);
        if (!remoteSeek || !input.seek(static_cast<qint64>(transferred)))
        {
            return remoteSeek ? failure(TransferExecutionErrorKind::LocalIo, transferred)
                              : remoteFailure(remoteSeek.error(), transferred);
        }
    }
    const auto closeAndRemoveTemporary = [&client, &temporaryPath, &remoteGuard] {
        [[maybe_unused]] const auto closed = remoteGuard.close({});
        [[maybe_unused]] const auto removed = client.removeEntry(temporaryPath, false, {});
    };

    std::array<char, transferBufferSize> buffer{};
    const auto total = static_cast<std::uint64_t>(sourceInfo.size());
    const auto started = std::chrono::steady_clock::now();
    const std::uint64_t startedAt = transferred;
    auto lastProgress = started;
    while (!stopToken.stop_requested())
    {
        const qint64 read = input.read(buffer.data(), static_cast<qint64>(buffer.size()));
        if (read < 0)
        {
            [[maybe_unused]] const auto closed = remoteGuard.close({});
            return failure(TransferExecutionErrorKind::LocalIo, transferred);
        }
        if (read == 0)
        {
            break;
        }
        auto written =
            client.writeFile(std::span<const char>(buffer.data(), static_cast<std::size_t>(read)), stopToken);
        if (!written)
        {
            [[maybe_unused]] const auto closed = remoteGuard.close({});
            return remoteFailure(written.error(), transferred);
        }
        transferred += static_cast<std::uint64_t>(read);
        const auto now = std::chrono::steady_clock::now();
        if (progress && (transferred == total || now - lastProgress >= progressInterval))
        {
            progress(transferred, total, speedSince(started, transferred - startedAt));
            lastProgress = now;
        }
    }
    if (stopToken.stop_requested())
    {
        [[maybe_unused]] const auto closed = remoteGuard.close({});
        const bool paused = pauseRequested && pauseRequested();
        if (!paused)
        {
            [[maybe_unused]] const auto removed = client.removeEntry(temporaryPath, false, {});
        }
        return TransferExecutionResult{.kind = paused ? TransferExecutionResultKind::Paused
                                                      : TransferExecutionResultKind::Cancelled,
                                       .transferredBytes = transferred,
                                       .finalDestinationPath = destination};
    }

    auto closed = remoteGuard.close(stopToken);
    if (!closed)
    {
        closeAndRemoveTemporary();
        return remoteFailure(closed.error(), transferred);
    }
    auto committed = client.renameEntry(temporaryPath, destination, replace, stopToken);
    if (!committed)
    {
        closeAndRemoveTemporary();
        return remoteFailure(committed.error(), transferred);
    }
    return TransferExecutionResult{.kind = TransferExecutionResultKind::Completed,
                                   .transferredBytes = transferred,
                                   .finalDestinationPath = destination};
}

} // namespace

QString localTransferPartialPath(const std::string_view destinationPath, const std::string_view taskId)
{
    return temporaryLocalPath(destinationPath, taskId);
}

std::string remoteTransferPartialPath(const std::string_view destinationPath, const std::string_view taskId)
{
    return temporaryRemotePath(destinationPath, taskId);
}

TransferExecutionResult executeTransfer(SftpClient &client, const TransferTask &task,
                                        const TransferExecutionOptions &options,
                                        const TransferProgressCallback &progress, const std::stop_token &stopToken,
                                        const TransferPauseRequestedCallback &pauseRequested)
{
    if (!validTransferTask(task))
    {
        return failure(TransferExecutionErrorKind::InvalidTask);
    }
    if (stopToken.stop_requested())
    {
        return TransferExecutionResult{.kind = TransferExecutionResultKind::Cancelled};
    }
    return task.direction == TransferDirection::Download
               ? executeDownload(client, task, options, progress, stopToken, pauseRequested)
               : executeUpload(client, task, options, progress, stopToken, pauseRequested);
}

} // namespace ztermy::sftp
