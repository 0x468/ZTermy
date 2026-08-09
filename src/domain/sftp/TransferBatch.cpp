#include "domain/sftp/TransferBatch.h"

#include <algorithm>
#include <limits>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ztermy::sftp
{
namespace
{

[[nodiscard]] bool containsNull(const std::string_view value) noexcept
{
    return value.find('\0') != std::string_view::npos;
}

[[nodiscard]] bool validIdentifier(const std::string_view value) noexcept
{
    return !value.empty() && value.size() <= 256 && !containsNull(value);
}

[[nodiscard]] bool validRootPath(const std::string_view value) noexcept
{
    return !value.empty() && value.size() <= maximumTransferPathUtf8Bytes && !containsNull(value);
}

[[nodiscard]] bool canTransition(const TransferPlanEntryStatus from, const TransferPlanEntryStatus to) noexcept
{
    switch (from)
    {
        case TransferPlanEntryStatus::Pending:
            return to == TransferPlanEntryStatus::Queued || to == TransferPlanEntryStatus::Skipped
                   || to == TransferPlanEntryStatus::Failed || to == TransferPlanEntryStatus::Cancelled
                   || to == TransferPlanEntryStatus::Completed;
        case TransferPlanEntryStatus::Queued:
            return to == TransferPlanEntryStatus::Running || to == TransferPlanEntryStatus::Cancelled
                   || to == TransferPlanEntryStatus::Interrupted;
        case TransferPlanEntryStatus::Running:
            return to == TransferPlanEntryStatus::Completed || to == TransferPlanEntryStatus::Failed
                   || to == TransferPlanEntryStatus::Cancelled || to == TransferPlanEntryStatus::Interrupted;
        case TransferPlanEntryStatus::Interrupted:
        case TransferPlanEntryStatus::Failed:
            return to == TransferPlanEntryStatus::Queued || to == TransferPlanEntryStatus::Cancelled;
        case TransferPlanEntryStatus::Completed:
        case TransferPlanEntryStatus::Skipped:
        case TransferPlanEntryStatus::Cancelled:
            return false;
    }
    return false;
}

[[nodiscard]] std::uint64_t saturatedAdd(const std::uint64_t left, const std::uint64_t right) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
    {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left + right;
}

} // namespace

bool validTransferRelativePath(const std::string_view path) noexcept
{
    if (path.empty() || path.size() > maximumTransferPathUtf8Bytes || containsNull(path) || path.front() == '/'
        || path.front() == '\\' || path.back() == '/' || path.back() == '\\')
    {
        return false;
    }
    std::size_t segmentStart = 0;
    for (std::size_t index = 0; index <= path.size(); ++index)
    {
        if (index != path.size() && path[index] != '/' && path[index] != '\\')
        {
            continue;
        }
        const std::string_view segment = path.substr(segmentStart, index - segmentStart);
        if (segment.empty() || segment == "." || segment == "..")
        {
            return false;
        }
        segmentStart = index + 1;
    }
    return true;
}

bool validTransferPlanEntry(const TransferPlanEntry &entry) noexcept
{
    if (!validIdentifier(entry.id) || !validTransferRelativePath(entry.relativePath) || !validRootPath(entry.sourcePath)
        || entry.depth > maximumTransferTreeDepth || entry.transferredBytes > entry.totalBytes
        || containsNull(entry.errorCode) || entry.errorCode.size() > 256)
    {
        return false;
    }
    if (entry.depth == 0 ? !entry.parentId.empty() : !validIdentifier(entry.parentId))
    {
        return false;
    }
    if (!entry.childTaskId.empty() && !validIdentifier(entry.childTaskId))
    {
        return false;
    }
    if (entry.kind != TransferPlanEntryKind::RegularFile
        && (entry.totalBytes != 0 || entry.transferredBytes != 0 || !entry.childTaskId.empty()))
    {
        return false;
    }
    if (entry.status == TransferPlanEntryStatus::Failed && entry.errorCode.empty())
    {
        return false;
    }
    if (entry.status != TransferPlanEntryStatus::Failed && !entry.errorCode.empty())
    {
        return false;
    }
    return true;
}

bool validTransferBatch(const TransferBatch &batch)
{
    if (!validIdentifier(batch.id) || !validIdentifier(batch.endpointId) || batch.displayName.empty()
        || batch.displayName.size() > 512 || containsNull(batch.displayName) || !validRootPath(batch.destinationRoot)
        || batch.sourceRoots.empty() || batch.sourceRoots.size() > maximumTransferSourceRoots
        || batch.entries.size() > maximumTransferPlanEntries || containsNull(batch.discoveryErrorCode)
        || batch.discoveryErrorCode.size() > 256)
    {
        return false;
    }
    if (std::ranges::any_of(batch.sourceRoots, [](const std::string &root) {
            return !validRootPath(root);
        }))
    {
        return false;
    }
    if (!batch.discoveryErrorCode.empty() && batch.status != TransferBatchStatus::Failed)
    {
        return false;
    }

    std::unordered_map<std::string_view, const TransferPlanEntry *> entries;
    entries.reserve(batch.entries.size());
    std::unordered_set<std::string_view> paths;
    paths.reserve(batch.entries.size());
    for (const TransferPlanEntry &entry : batch.entries)
    {
        if (!validTransferPlanEntry(entry) || !entries.emplace(entry.id, &entry).second
            || !paths.emplace(entry.relativePath).second)
        {
            return false;
        }
        if (entry.depth == 0)
        {
            continue;
        }
        const auto parent = entries.find(entry.parentId);
        if (parent == entries.end() || parent->second->kind != TransferPlanEntryKind::Directory
            || parent->second->depth + 1 != entry.depth)
        {
            return false;
        }
    }
    return true;
}

std::expected<void, TransferBatchError> appendTransferPlanEntry(TransferBatch &batch, TransferPlanEntry entry)
{
    if (batch.status != TransferBatchStatus::Discovering)
    {
        return std::unexpected(TransferBatchError::InvalidTransition);
    }
    if (batch.entries.size() >= maximumTransferPlanEntries)
    {
        return std::unexpected(TransferBatchError::EntryLimit);
    }
    if (!validTransferPlanEntry(entry))
    {
        return std::unexpected(TransferBatchError::InvalidEntry);
    }
    if (std::ranges::any_of(batch.entries, [&](const TransferPlanEntry &current) {
            return current.id == entry.id;
        }))
    {
        return std::unexpected(TransferBatchError::DuplicateEntryId);
    }
    if (std::ranges::any_of(batch.entries, [&](const TransferPlanEntry &current) {
            return current.relativePath == entry.relativePath;
        }))
    {
        return std::unexpected(TransferBatchError::DuplicateRelativePath);
    }
    if (entry.depth != 0)
    {
        const auto parent = std::ranges::find(batch.entries, entry.parentId, &TransferPlanEntry::id);
        if (parent == batch.entries.end())
        {
            return std::unexpected(TransferBatchError::MissingParent);
        }
        if (parent->kind != TransferPlanEntryKind::Directory)
        {
            return std::unexpected(TransferBatchError::ParentNotDirectory);
        }
        if (parent->depth + 1 != entry.depth)
        {
            return std::unexpected(TransferBatchError::DepthMismatch);
        }
    }
    batch.entries.push_back(std::move(entry));
    return {};
}

std::expected<void, TransferBatchError> updateTransferPlanEntry(TransferBatch &batch, const std::string_view entryId,
                                                                const TransferPlanEntryStatus status,
                                                                const std::uint64_t transferredBytes,
                                                                std::string errorCode)
{
    const auto found = std::ranges::find(batch.entries, entryId, &TransferPlanEntry::id);
    if (found == batch.entries.end())
    {
        return std::unexpected(TransferBatchError::InvalidEntry);
    }
    if (!canTransition(found->status, status) || transferredBytes > found->totalBytes
        || (status == TransferPlanEntryStatus::Failed) != !errorCode.empty() || containsNull(errorCode)
        || errorCode.size() > 256)
    {
        return std::unexpected(TransferBatchError::InvalidTransition);
    }
    found->status = status;
    found->transferredBytes = transferredBytes;
    found->errorCode = std::move(errorCode);
    if (status == TransferPlanEntryStatus::Completed)
    {
        found->transferredBytes = found->totalBytes;
    }
    return {};
}

std::expected<void, TransferBatchError> finalizeTransferDiscovery(TransferBatch &batch, std::string errorCode)
{
    if (batch.status != TransferBatchStatus::Discovering || containsNull(errorCode) || errorCode.size() > 256)
    {
        return std::unexpected(TransferBatchError::InvalidTransition);
    }
    batch.discoveryErrorCode = std::move(errorCode);
    batch.status = batch.discoveryErrorCode.empty() ? TransferBatchStatus::Ready : TransferBatchStatus::Failed;
    for (TransferPlanEntry &entry : batch.entries)
    {
        if ((entry.kind == TransferPlanEntryKind::SymbolicLink || entry.kind == TransferPlanEntryKind::Unsupported)
            && entry.status == TransferPlanEntryStatus::Pending)
        {
            entry.status = TransferPlanEntryStatus::Skipped;
        }
    }
    return {};
}

TransferBatchSummary summarizeTransferBatch(const TransferBatch &batch) noexcept
{
    TransferBatchSummary summary;
    summary.entryCount = batch.entries.size();
    summary.discoveryComplete = batch.status != TransferBatchStatus::Discovering;
    summary.terminal = batch.status == TransferBatchStatus::Completed || batch.status == TransferBatchStatus::Failed
                       || batch.status == TransferBatchStatus::Cancelled;
    for (const TransferPlanEntry &entry : batch.entries)
    {
        summary.directoryCount += entry.kind == TransferPlanEntryKind::Directory ? 1U : 0U;
        summary.regularFileCount += entry.kind == TransferPlanEntryKind::RegularFile ? 1U : 0U;
        summary.completedCount += entry.status == TransferPlanEntryStatus::Completed ? 1U : 0U;
        summary.skippedCount += entry.status == TransferPlanEntryStatus::Skipped ? 1U : 0U;
        summary.failedCount += entry.status == TransferPlanEntryStatus::Failed ? 1U : 0U;
        summary.activeCount +=
            entry.status == TransferPlanEntryStatus::Queued || entry.status == TransferPlanEntryStatus::Running ? 1U
                                                                                                                : 0U;
        if (entry.kind == TransferPlanEntryKind::RegularFile)
        {
            summary.totalBytes = saturatedAdd(summary.totalBytes, entry.totalBytes);
            summary.transferredBytes = saturatedAdd(summary.transferredBytes, entry.transferredBytes);
        }
    }
    return summary;
}

} // namespace ztermy::sftp
