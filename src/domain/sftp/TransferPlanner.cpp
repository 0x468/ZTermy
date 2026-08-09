#include "domain/sftp/TransferPlanner.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace ztermy::sftp
{
namespace
{

struct PendingNode final
{
    TransferSourceNode node;
    std::string parentId;
    std::string relativePath;
    std::uint32_t depth = 0;
};

[[nodiscard]] bool validNodeName(const std::string_view name) noexcept
{
    return !name.empty() && name != "." && name != ".." && name.find('/') == std::string_view::npos
           && name.find('\\') == std::string_view::npos && name.find('\0') == std::string_view::npos;
}

[[nodiscard]] TransferPlanEntryKind planKind(const EntryType type) noexcept
{
    switch (type)
    {
        case EntryType::Directory:
            return TransferPlanEntryKind::Directory;
        case EntryType::RegularFile:
            return TransferPlanEntryKind::RegularFile;
        case EntryType::SymbolicLink:
            return TransferPlanEntryKind::SymbolicLink;
        case EntryType::Other:
            return TransferPlanEntryKind::Unsupported;
    }
    return TransferPlanEntryKind::Unsupported;
}

[[nodiscard]] std::string entryId(const std::size_t index)
{
    return "entry-" + std::to_string(index + 1);
}

[[nodiscard]] TransferPlanningError mapAppendError(const TransferBatchError error) noexcept
{
    switch (error)
    {
        case TransferBatchError::DuplicateRelativePath:
            return TransferPlanningError::DuplicateDestination;
        case TransferBatchError::EntryLimit:
            return TransferPlanningError::EntryLimit;
        case TransferBatchError::DepthMismatch:
            return TransferPlanningError::DepthLimit;
        case TransferBatchError::InvalidBatch:
        case TransferBatchError::InvalidEntry:
        case TransferBatchError::DuplicateEntryId:
        case TransferBatchError::MissingParent:
        case TransferBatchError::ParentNotDirectory:
        case TransferBatchError::InvalidTransition:
            return TransferPlanningError::InvalidSource;
    }
    return TransferPlanningError::InvalidSource;
}

} // namespace

std::expected<TransferBatch, TransferPlanningError>
planTransferTree(const TransferPlanRequest &request, TransferSourceTree &source, const std::stop_token &stopToken)
{
    TransferBatch batch{.id = request.batchId,
                        .endpointId = request.endpointId,
                        .displayName = request.displayName,
                        .destinationRoot = request.destinationRoot,
                        .sourceRoots = request.sourceRoots,
                        .direction = request.direction,
                        .conflictPolicy = request.conflictPolicy};
    if (!validTransferBatch(batch))
    {
        return std::unexpected(TransferPlanningError::InvalidRequest);
    }

    std::vector<PendingNode> pending;
    pending.reserve(request.sourceRoots.size());
    for (auto root = request.sourceRoots.rbegin(); root != request.sourceRoots.rend(); ++root)
    {
        if (stopToken.stop_requested())
        {
            return std::unexpected(TransferPlanningError::Cancelled);
        }
        auto node = source.stat(*root, stopToken);
        if (!node)
        {
            return std::unexpected(TransferPlanningError::SourceUnavailable);
        }
        if (!validNodeName(node->name) || node->sourcePath.empty())
        {
            return std::unexpected(TransferPlanningError::InvalidSource);
        }
        std::string rootName = node->name;
        pending.push_back(PendingNode{.node = std::move(*node), .relativePath = std::move(rootName)});
    }

    while (!pending.empty())
    {
        if (stopToken.stop_requested())
        {
            return std::unexpected(TransferPlanningError::Cancelled);
        }
        PendingNode current = std::move(pending.back());
        pending.pop_back();
        if (current.depth > maximumTransferTreeDepth)
        {
            return std::unexpected(TransferPlanningError::DepthLimit);
        }

        TransferPlanEntry entry{.id = entryId(batch.entries.size()),
                                .parentId = current.parentId,
                                .relativePath = current.relativePath,
                                .sourcePath = current.node.sourcePath,
                                .kind = planKind(current.node.type),
                                .totalBytes = current.node.type == EntryType::RegularFile ? current.node.size : 0,
                                .depth = current.depth,
                                .sourceModifiedUtcSeconds = current.node.modifiedUtcSeconds};
        const std::string currentId = entry.id;
        if (auto appended = appendTransferPlanEntry(batch, std::move(entry)); !appended)
        {
            return std::unexpected(mapAppendError(appended.error()));
        }
        if (current.node.type != EntryType::Directory)
        {
            continue;
        }
        if (current.depth == maximumTransferTreeDepth)
        {
            auto children = source.list(current.node.sourcePath, stopToken);
            if (!children)
            {
                return std::unexpected(TransferPlanningError::SourceUnavailable);
            }
            if (!children->empty())
            {
                return std::unexpected(TransferPlanningError::DepthLimit);
            }
            continue;
        }

        auto children = source.list(current.node.sourcePath, stopToken);
        if (!children)
        {
            return std::unexpected(TransferPlanningError::SourceUnavailable);
        }
        std::ranges::sort(*children, {}, &TransferSourceNode::name);
        for (auto child = children->rbegin(); child != children->rend(); ++child)
        {
            if (!validNodeName(child->name) || child->sourcePath.empty())
            {
                return std::unexpected(TransferPlanningError::InvalidSource);
            }
            std::string relativePath = current.relativePath + '/' + child->name;
            pending.push_back(PendingNode{.node = std::move(*child),
                                          .parentId = currentId,
                                          .relativePath = std::move(relativePath),
                                          .depth = current.depth + 1});
        }
        if (batch.entries.size() + pending.size() > maximumTransferPlanEntries)
        {
            return std::unexpected(TransferPlanningError::EntryLimit);
        }
    }

    if (auto finalized = finalizeTransferDiscovery(batch); !finalized)
    {
        return std::unexpected(TransferPlanningError::InvalidSource);
    }
    return batch;
}

} // namespace ztermy::sftp
