#include "application/sftp/TransferSourceAdapters.h"

#include "domain/sftp/SftpTypes.h"

#include <QDir>
#include <QFileInfo>
#include <QString>

#include <algorithm>
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

[[nodiscard]] TransferSourceNode localNode(const QFileInfo &info)
{
    EntryType type = EntryType::Other;
    if (info.isSymLink())
    {
        type = EntryType::SymbolicLink;
    }
    else if (info.isDir())
    {
        type = EntryType::Directory;
    }
    else if (info.isFile())
    {
        type = EntryType::RegularFile;
    }
    return {.sourcePath = utf8String(info.absoluteFilePath()),
            .name = utf8String(info.fileName()),
            .type = type,
            .size = type == EntryType::RegularFile ? static_cast<std::uint64_t>(info.size()) : 0,
            .modifiedUtcSeconds = info.lastModified().isValid()
                                      ? std::optional<std::int64_t>{info.lastModified().toSecsSinceEpoch()}
                                      : std::nullopt};
}

[[nodiscard]] TransferSourceNode remoteNode(DirectoryEntry entry)
{
    return {.sourcePath = std::move(entry.remotePath),
            .name = std::move(entry.name),
            .type = entry.type,
            .size = entry.size,
            .modifiedUtcSeconds = entry.modifiedUtcSeconds};
}

[[nodiscard]] TransferSourceError sourceError(const ssh::SshTransportError &error)
{
    return {.code = "remote-" + std::to_string(static_cast<unsigned int>(error.kind))};
}

} // namespace

std::expected<TransferSourceNode, TransferSourceError> LocalTransferSourceTree::stat(const std::string_view sourcePath,
                                                                                     const std::stop_token &stopToken)
{
    if (stopToken.stop_requested())
    {
        return std::unexpected(TransferSourceError{.code = "cancelled"});
    }
    const QFileInfo info(utf8QString(sourcePath));
    if (!info.exists() && !info.isSymLink())
    {
        return std::unexpected(TransferSourceError{.code = "not-found"});
    }
    TransferSourceNode node = localNode(info);
    if (node.name.empty())
    {
        return std::unexpected(TransferSourceError{.code = "invalid-name"});
    }
    return node;
}

std::expected<std::vector<TransferSourceNode>, TransferSourceError>
LocalTransferSourceTree::list(const std::string_view sourcePath, const std::stop_token &stopToken)
{
    if (stopToken.stop_requested())
    {
        return std::unexpected(TransferSourceError{.code = "cancelled"});
    }
    const QDir directory(utf8QString(sourcePath));
    if (!directory.exists())
    {
        return std::unexpected(TransferSourceError{.code = "not-found"});
    }
    const QFileInfoList entries = directory.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name | QDir::DirsFirst);
    std::vector<TransferSourceNode> result;
    result.reserve(static_cast<std::size_t>(entries.size()));
    for (const QFileInfo &entry : entries)
    {
        if (stopToken.stop_requested())
        {
            return std::unexpected(TransferSourceError{.code = "cancelled"});
        }
        result.push_back(localNode(entry));
    }
    return result;
}

RemoteTransferSourceTree::RemoteTransferSourceTree(SftpClient &client) noexcept : m_client(client) {}

std::expected<TransferSourceNode, TransferSourceError> RemoteTransferSourceTree::stat(const std::string_view sourcePath,
                                                                                      const std::stop_token &stopToken)
{
    auto entry = m_client.statEntry(sourcePath, stopToken);
    if (!entry)
    {
        return std::unexpected(sourceError(entry.error()));
    }
    if (!*entry)
    {
        return std::unexpected(TransferSourceError{.code = "not-found"});
    }
    TransferSourceNode node = remoteNode(std::move(**entry));
    if (node.name.empty())
    {
        const std::string normalized = normalizeRemotePath(sourcePath).value_or(std::string(sourcePath));
        const std::size_t separator = normalized.find_last_of('/');
        node.name = normalized.substr(separator == std::string::npos ? 0 : separator + 1);
    }
    return node;
}

std::expected<std::vector<TransferSourceNode>, TransferSourceError>
RemoteTransferSourceTree::list(const std::string_view sourcePath, const std::stop_token &stopToken)
{
    auto entries = m_client.listDirectory(sourcePath, stopToken);
    if (!entries)
    {
        return std::unexpected(sourceError(entries.error()));
    }
    std::vector<TransferSourceNode> result;
    result.reserve(entries->size());
    for (DirectoryEntry &entry : *entries)
    {
        if (stopToken.stop_requested())
        {
            return std::unexpected(TransferSourceError{.code = "cancelled"});
        }
        if (entry.name == "." || entry.name == "..")
        {
            continue;
        }
        result.push_back(remoteNode(std::move(entry)));
    }
    return result;
}

} // namespace ztermy::sftp
