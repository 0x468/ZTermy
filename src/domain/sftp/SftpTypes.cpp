#include "domain/sftp/SftpTypes.h"

#include <vector>

namespace ztermy::sftp
{
namespace
{

[[nodiscard]] bool containsNull(const std::string_view value) noexcept
{
    return value.find('\0') != std::string_view::npos;
}

} // namespace

std::expected<std::string, RemotePathError> normalizeRemotePath(const std::string_view path)
{
    if (path.empty())
    {
        return std::unexpected(RemotePathError::Empty);
    }
    if (path.front() != '/')
    {
        return std::unexpected(RemotePathError::NotAbsolute);
    }
    if (containsNull(path))
    {
        return std::unexpected(RemotePathError::ContainsNull);
    }

    std::vector<std::string_view> components;
    std::size_t offset = 1;
    while (offset <= path.size())
    {
        const std::size_t separator = path.find('/', offset);
        const std::size_t end = separator == std::string_view::npos ? path.size() : separator;
        const std::string_view component = path.substr(offset, end - offset);
        if (!component.empty() && component != ".")
        {
            if (component == "..")
            {
                if (components.empty())
                {
                    return std::unexpected(RemotePathError::TraversesAboveRoot);
                }
                components.pop_back();
            }
            else
            {
                components.push_back(component);
            }
        }
        if (separator == std::string_view::npos)
        {
            break;
        }
        offset = separator + 1;
    }

    std::string normalized = "/";
    for (std::size_t index = 0; index < components.size(); ++index)
    {
        if (index != 0)
        {
            normalized.push_back('/');
        }
        normalized.append(components[index]);
    }
    return normalized;
}

std::expected<std::string, RemotePathError> joinRemotePath(const std::string_view directory,
                                                           const std::string_view name)
{
    if (!validRemoteName(name))
    {
        return std::unexpected(RemotePathError::InvalidName);
    }
    auto normalizedDirectory = normalizeRemotePath(directory);
    if (!normalizedDirectory)
    {
        return std::unexpected(normalizedDirectory.error());
    }

    std::string joined = std::move(*normalizedDirectory);
    if (joined != "/")
    {
        joined.push_back('/');
    }
    joined.append(name);
    return joined;
}

std::string parentRemotePath(const std::string_view normalizedPath)
{
    const auto path = normalizeRemotePath(normalizedPath);
    if (!path || *path == "/")
    {
        return "/";
    }
    const std::size_t separator = path->find_last_of('/');
    return separator == 0 ? std::string{"/"} : path->substr(0, separator);
}

bool validRemoteName(const std::string_view name) noexcept
{
    return !name.empty() && name != "." && name != ".." && name.find('/') == std::string_view::npos
           && !containsNull(name);
}

bool hiddenRemoteName(const std::string_view name) noexcept
{
    return name.size() > 1 && name.front() == '.' && name != "..";
}

bool validTransferTask(const TransferTask &task) noexcept
{
    if (task.id.empty() || task.endpointId.empty() || task.displayName.empty() || task.sourcePath.empty()
        || task.destinationPath.empty())
    {
        return false;
    }
    if (containsNull(task.id) || containsNull(task.endpointId) || containsNull(task.displayName)
        || containsNull(task.sourcePath) || containsNull(task.destinationPath) || containsNull(task.errorCode))
    {
        return false;
    }
    if (task.transferredBytes > task.totalBytes && task.totalBytes != 0)
    {
        return false;
    }
    if (task.status == TransferStatus::Completed && task.totalBytes != 0 && task.transferredBytes != task.totalBytes)
    {
        return false;
    }
    return true;
}

bool canTransition(const TransferStatus from, const TransferStatus to) noexcept
{
    if (from == to)
    {
        return true;
    }

    switch (from)
    {
        case TransferStatus::Queued:
            return to == TransferStatus::Running || to == TransferStatus::NeedsAttention
                   || to == TransferStatus::Cancelled;
        case TransferStatus::Running:
            return to == TransferStatus::NeedsAttention || to == TransferStatus::Completed
                   || to == TransferStatus::Failed || to == TransferStatus::Cancelled;
        case TransferStatus::NeedsAttention:
            return to == TransferStatus::Queued || to == TransferStatus::Cancelled;
        case TransferStatus::Failed:
            return to == TransferStatus::Queued;
        case TransferStatus::Completed:
        case TransferStatus::Cancelled:
            return false;
    }
    return false;
}

bool canReplaceConflict(const FileConflict &conflict) noexcept
{
    const bool sourceDirectory = conflict.sourceType == EntryType::Directory;
    const bool destinationDirectory = conflict.destinationType == EntryType::Directory;
    if (sourceDirectory != destinationDirectory)
    {
        return false;
    }
    return conflict.sourceType != EntryType::SymbolicLink && conflict.destinationType != EntryType::SymbolicLink;
}

} // namespace ztermy::sftp
