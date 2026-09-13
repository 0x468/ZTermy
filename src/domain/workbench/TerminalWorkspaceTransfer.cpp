#include "domain/workbench/TerminalWorkspaceTransfer.h"

#include <algorithm>
#include <utility>

namespace ztermy::workbench
{
namespace
{
TerminalWorkspaceLayout *workspace(WorkspaceState &state, const std::string &id)
{
    const auto found = std::ranges::find(state.terminalWorkspaces, id, &TerminalWorkspaceLayout::id);
    return found == state.terminalWorkspaces.end() ? nullptr : &*found;
}

const TerminalLayoutNode *leaf(const TerminalWorkspaceLayout &layout, const std::string &id)
{
    const auto found = std::ranges::find(layout.nodes, id, &TerminalLayoutNode::id);
    return found == layout.nodes.end() || found->kind != TerminalLayoutNodeKind::Leaf ? nullptr : &*found;
}

void replaceChild(TerminalWorkspaceLayout &layout, const std::string &before, const std::string &after)
{
    if (layout.rootNodeId == before)
        layout.rootNodeId = after;
    for (auto &node : layout.nodes)
    {
        if (node.firstChildId == before)
            node.firstChildId = after;
        if (node.secondChildId == before)
            node.secondChildId = after;
    }
}

bool insertTree(TerminalWorkspaceLayout &target, const TerminalWorkspaceLayout &source,
                const TerminalWorkspaceTransfer &transfer)
{
    if (!leaf(target, transfer.targetPaneId) || transfer.splitNodeId.empty())
        return false;
    replaceChild(target, transfer.targetPaneId, transfer.splitNodeId);
    target.nodes.insert(target.nodes.end(), source.nodes.begin(), source.nodes.end());
    target.restoreIntents.insert(target.restoreIntents.end(), source.restoreIntents.begin(),
                                 source.restoreIntents.end());
    target.nodes.push_back({.id = transfer.splitNodeId,
                            .firstChildId = transfer.placeAfter ? transfer.targetPaneId : source.rootNodeId,
                            .secondChildId = transfer.placeAfter ? source.rootNodeId : transfer.targetPaneId,
                            .kind = TerminalLayoutNodeKind::Split,
                            .orientation = transfer.orientation});
    target.activePaneId = source.activePaneId;
    return validTerminalWorkspaceLayout(target);
}

bool swapLeaves(TerminalWorkspaceLayout &source, TerminalWorkspaceLayout &target,
                const TerminalWorkspaceTransfer &transfer)
{
    const auto *first = leaf(source, transfer.sourcePaneId);
    const auto *second = leaf(target, transfer.targetPaneId);
    if (!first || !second)
        return false;
    if (&source == &target)
    {
        // Swap tree edges, never leaf IDs or their session/restore bindings.
        for (auto &node : source.nodes)
        {
            for (auto *child : {&node.firstChildId, &node.secondChildId})
            {
                if (*child == transfer.sourcePaneId)
                    *child = transfer.targetPaneId;
                else if (*child == transfer.targetPaneId)
                    *child = transfer.sourcePaneId;
            }
        }
    }
    else
    {
        const auto firstNode = *first;
        const auto secondNode = *second;
        const auto firstIntent =
            std::ranges::find(source.restoreIntents, firstNode.restoreIntentId, &TerminalRestoreIntent::id);
        const auto secondIntent =
            std::ranges::find(target.restoreIntents, secondNode.restoreIntentId, &TerminalRestoreIntent::id);
        std::swap(*firstIntent, *secondIntent);
        *std::ranges::find(source.nodes, firstNode.id, &TerminalLayoutNode::id) = secondNode;
        *std::ranges::find(target.nodes, secondNode.id, &TerminalLayoutNode::id) = firstNode;
        replaceChild(source, firstNode.id, secondNode.id);
        replaceChild(target, secondNode.id, firstNode.id);
        if (source.activePaneId == firstNode.id)
            source.activePaneId = secondNode.id;
    }
    target.activePaneId = transfer.sourcePaneId;
    return true;
}

bool applyTransfer(WorkspaceState &state, const TerminalWorkspaceTransfer &transfer)
{
    auto *source = workspace(state, transfer.sourceWorkspaceId);
    auto *target = workspace(state, transfer.targetWorkspaceId);
    if (!source)
        return false;
    if (transfer.kind == TerminalTransferKind::SwapPanes)
        return target && swapLeaves(*source, *target, transfer);
    if (transfer.kind == TerminalTransferKind::MergeWorkspace)
    {
        if (!target || source == target || !insertTree(*target, *source, transfer))
            return false;
        std::erase_if(state.terminalWorkspaces, [&](const auto &entry) {
            return entry.id == transfer.sourceWorkspaceId;
        });
        return true;
    }
    if (transfer.kind == TerminalTransferKind::MovePane && source == target)
    {
        if (!moveTerminalPane(*source, transfer.sourcePaneId, transfer.targetPaneId, transfer.splitNodeId,
                              transfer.orientation, transfer.placeAfter))
            return false;
        source->activePaneId = transfer.sourcePaneId;
        return true;
    }
    const auto *pane = leaf(*source, transfer.sourcePaneId);
    if (!pane || (transfer.kind == TerminalTransferKind::ExtractPane ? target != nullptr : target == nullptr))
        return false;
    const auto intent = *std::ranges::find(source->restoreIntents, pane->restoreIntentId, &TerminalRestoreIntent::id);
    auto extracted = makeSinglePaneTerminalWorkspace(transfer.targetWorkspaceId, pane->id, intent);
    if (source->restoreIntents.size() > 1)
    {
        if (!closeTerminalPane(*source, pane->id))
            return false;
    }
    else
    {
        std::erase_if(state.terminalWorkspaces, [&](const auto &entry) {
            return entry.id == transfer.sourceWorkspaceId;
        });
    }
    // Erasure can relocate the target in the vector.
    target = workspace(state, transfer.targetWorkspaceId);
    if (transfer.kind == TerminalTransferKind::ExtractPane)
    {
        state.terminalWorkspaces.push_back(std::move(extracted));
        return true;
    }
    return target && insertTree(*target, extracted, transfer);
}
} // namespace

bool transferTerminalWorkspace(WorkspaceState &state, const TerminalWorkspaceTransfer &transfer)
{
    if (!validWorkspaceState(state) || transfer.sourceWorkspaceId.empty() || transfer.targetWorkspaceId.empty())
        return false;
    auto candidate = state;
    if (!applyTransfer(candidate, transfer))
        return false;
    candidate.activeTerminalWorkspaceId = transfer.targetWorkspaceId;
    if (!validWorkspaceState(candidate))
        return false;
    state = std::move(candidate);
    return true;
}

} // namespace ztermy::workbench
