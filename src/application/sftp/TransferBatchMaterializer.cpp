#include "application/sftp/TransferBatchMaterializer.h"

#include <QDir>
#include <QFileInfo>
#include <QString>

#include <ranges>
#include <utility>

namespace ztermy::sftp
{
namespace
{

[[nodiscard]] std::string utf8String(const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] QString utf8QString(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::expected<std::string, TransferMaterializationError> localDestination(const TransferBatch &batch,
                                                                                        const TransferPlanEntry &entry)
{
    const QDir root(utf8QString(batch.destinationRoot));
    const QString rootPath = QDir::fromNativeSeparators(QDir::cleanPath(root.absolutePath()));
    const QString destination =
        QDir::fromNativeSeparators(QDir::cleanPath(root.absoluteFilePath(utf8QString(entry.relativePath))));
    const QString prefix = rootPath.endsWith('/') ? rootPath : rootPath + '/';
    if (destination != rootPath && !destination.startsWith(prefix, Qt::CaseInsensitive))
    {
        return std::unexpected(TransferMaterializationError::InvalidDestination);
    }
    return utf8String(destination);
}

[[nodiscard]] std::expected<std::string, TransferMaterializationError> remoteDestination(const TransferBatch &batch,
                                                                                         const TransferPlanEntry &entry)
{
    auto root = normalizeRemotePath(batch.destinationRoot);
    if (!root)
    {
        return std::unexpected(TransferMaterializationError::InvalidDestination);
    }
    if (*root != "/")
    {
        root->push_back('/');
    }
    root->append(entry.relativePath);
    auto destination = normalizeRemotePath(*root);
    return destination ? std::expected<std::string, TransferMaterializationError>{std::move(*destination)}
                       : std::unexpected(TransferMaterializationError::InvalidDestination);
}

[[nodiscard]] std::expected<void, TransferMaterializationError> ensureLocalDirectory(const std::string_view path)
{
    if (!QDir().mkpath(utf8QString(path)))
    {
        return std::unexpected(TransferMaterializationError::LocalIo);
    }
    return {};
}

[[nodiscard]] std::expected<void, TransferMaterializationError>
ensureRemoteDirectory(SftpClient &client, const std::string_view path, const std::stop_token &stopToken)
{
    auto existing = client.statEntry(path, stopToken);
    if (!existing)
    {
        return std::unexpected(TransferMaterializationError::RemoteIo);
    }
    if (*existing)
    {
        return (*existing)->type == EntryType::Directory
                   ? std::expected<void, TransferMaterializationError>{}
                   : std::unexpected(TransferMaterializationError::DestinationConflict);
    }
    if (auto created = client.createDirectory(path, stopToken); !created)
    {
        return std::unexpected(TransferMaterializationError::RemoteIo);
    }
    return {};
}

[[nodiscard]] std::string displayName(const TransferPlanEntry &entry)
{
    const std::size_t separator = entry.relativePath.find_last_of("/\\");
    return entry.relativePath.substr(separator == std::string::npos ? 0 : separator + 1);
}

} // namespace

std::expected<std::vector<TransferTask>, TransferMaterializationError>
materializeTransferBatch(TransferBatch &batch, SftpClient *remoteClient, const std::string_view filenameEncoding,
                         const std::stop_token &stopToken)
{
    if (!validTransferBatch(batch) || batch.status != TransferBatchStatus::Ready || filenameEncoding.empty()
        || (batch.direction == TransferBatchDirection::Upload && remoteClient == nullptr))
    {
        return std::unexpected(TransferMaterializationError::InvalidBatch);
    }

    std::vector<TransferTask> tasks;
    tasks.reserve(summarizeTransferBatch(batch).regularFileCount);
    for (TransferPlanEntry &entry : batch.entries)
    {
        if (stopToken.stop_requested())
        {
            batch.status = TransferBatchStatus::Interrupted;
            return std::unexpected(TransferMaterializationError::Cancelled);
        }
        const auto destination = batch.direction == TransferBatchDirection::Upload ? remoteDestination(batch, entry)
                                                                                   : localDestination(batch, entry);
        if (!destination)
        {
            batch.status = TransferBatchStatus::Failed;
            return std::unexpected(destination.error());
        }
        if (entry.kind == TransferPlanEntryKind::Directory)
        {
            const auto created = batch.direction == TransferBatchDirection::Upload
                                     ? ensureRemoteDirectory(*remoteClient, *destination, stopToken)
                                     : ensureLocalDirectory(*destination);
            if (!created)
            {
                entry.status = TransferPlanEntryStatus::Failed;
                entry.errorCode = "directory-materialization";
                batch.status = TransferBatchStatus::Failed;
                return std::unexpected(created.error());
            }
            entry.status = TransferPlanEntryStatus::Completed;
            continue;
        }
        if (entry.kind != TransferPlanEntryKind::RegularFile)
        {
            continue;
        }

        entry.childTaskId = batch.id + ':' + entry.id;
        entry.status = TransferPlanEntryStatus::Queued;
        tasks.push_back(TransferTask{.id = entry.childTaskId,
                                     .endpointId = batch.endpointId,
                                     .displayName = displayName(entry),
                                     .sourcePath = entry.sourcePath,
                                     .destinationPath = *destination,
                                     .filenameEncoding = std::string(filenameEncoding),
                                     .direction = batch.direction == TransferBatchDirection::Upload
                                                      ? TransferDirection::Upload
                                                      : TransferDirection::Download,
                                     .totalBytes = entry.totalBytes,
                                     .sourceModifiedUtcSeconds = entry.sourceModifiedUtcSeconds});
    }
    batch.status = tasks.empty() ? TransferBatchStatus::Completed : TransferBatchStatus::Running;
    return tasks;
}

} // namespace ztermy::sftp
