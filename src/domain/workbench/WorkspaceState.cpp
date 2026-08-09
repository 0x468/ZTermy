#include "domain/workbench/WorkspaceState.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <ranges>
#include <utility>

namespace ztermy::workbench
{
namespace
{

bool validRemotePath(const std::string_view path) noexcept
{
    return !path.empty() && path.size() <= 4096 && path.front() == '/' && path.find('\0') == std::string_view::npos;
}

bool validPage(const std::string_view page) noexcept
{
    return page == "history" || page == "scripts" || page == "sftp";
}

bool uniqueRemotePaths(const std::vector<std::string> &paths) noexcept
{
    for (auto current = paths.begin(); current != paths.end(); ++current)
    {
        if (std::find(std::next(current), paths.end(), *current) != paths.end())
        {
            return false;
        }
    }
    return true;
}

bool validBoundedText(const std::string_view value, const std::size_t maximumLength, const bool allowEmpty) noexcept
{
    return (allowEmpty || !value.empty()) && value.size() <= maximumLength
           && value.find('\0') == std::string_view::npos;
}

const TerminalLayoutNode *findTerminalNode(const TerminalWorkspaceLayout &layout, const std::string_view id) noexcept
{
    const auto found = std::ranges::find(layout.nodes, id, &TerminalLayoutNode::id);
    return found == layout.nodes.end() ? nullptr : &*found;
}

TerminalLayoutNode *findTerminalNode(TerminalWorkspaceLayout &layout, const std::string_view id) noexcept
{
    const auto found = std::ranges::find(layout.nodes, id, &TerminalLayoutNode::id);
    return found == layout.nodes.end() ? nullptr : &*found;
}

const TerminalRestoreIntent *findRestoreIntent(const TerminalWorkspaceLayout &layout,
                                               const std::string_view id) noexcept
{
    const auto found = std::ranges::find(layout.restoreIntents, id, &TerminalRestoreIntent::id);
    return found == layout.restoreIntents.end() ? nullptr : &*found;
}

bool validRestoreIntent(const TerminalRestoreIntent &intent) noexcept
{
    if (!validBoundedText(intent.id, 128, false) || !validBoundedText(intent.title, 256, true)
        || !validBoundedText(intent.profileId, 256, true))
    {
        return false;
    }
    return intent.kind == TerminalRestoreKind::Local ? intent.profileId.empty() : !intent.profileId.empty();
}

bool uniqueTerminalIds(const TerminalWorkspaceLayout &layout) noexcept
{
    for (auto current = layout.nodes.begin(); current != layout.nodes.end(); ++current)
    {
        if (std::ranges::find(std::next(current), layout.nodes.end(), current->id, &TerminalLayoutNode::id)
            != layout.nodes.end())
        {
            return false;
        }
    }
    for (auto current = layout.restoreIntents.begin(); current != layout.restoreIntents.end(); ++current)
    {
        if (std::ranges::find(std::next(current), layout.restoreIntents.end(), current->id, &TerminalRestoreIntent::id)
            != layout.restoreIntents.end())
        {
            return false;
        }
    }
    return true;
}

TerminalLayoutNode *findParentNode(TerminalWorkspaceLayout &layout, const std::string_view childId) noexcept
{
    const auto found = std::ranges::find_if(layout.nodes, [childId](const TerminalLayoutNode &node) {
        return node.kind == TerminalLayoutNodeKind::Split
               && (node.firstChildId == childId || node.secondChildId == childId);
    });
    return found == layout.nodes.end() ? nullptr : &*found;
}

} // namespace

bool validProfileWorkspaceState(const ProfileWorkspaceState &state) noexcept
{
    if (state.profileId.empty() || state.profileId.size() > 256 || !validRemotePath(state.lastRemotePath)
        || state.recentRemotePaths.size() > maximumRecentRemotePaths
        || state.bookmarkedRemotePaths.size() > maximumBookmarkedRemotePaths || !validPage(state.workbenchPage)
        || (state.workbenchSide != "left" && state.workbenchSide != "right") || !std::isfinite(state.workbenchWidth)
        || (state.sftpViewMode != "list" && state.sftpViewMode != "tree")
        || (state.sftpSortColumn != "name" && state.sftpSortColumn != "modified" && state.sftpSortColumn != "size"
            && state.sftpSortColumn != "type")
        || (state.sftpFilenameEncoding != "utf-8" && state.sftpFilenameEncoding != "gb18030")
        || state.workbenchWidth < 320.0 || state.workbenchWidth > 960.0 || !std::isfinite(state.composerHeight)
        || state.composerHeight < 96.0 || state.composerHeight > 480.0)
    {
        return false;
    }
    return std::ranges::all_of(state.recentRemotePaths, validRemotePath)
           && std::ranges::all_of(state.bookmarkedRemotePaths, validRemotePath)
           && uniqueRemotePaths(state.bookmarkedRemotePaths);
}

bool validWorkspaceState(const WorkspaceState &state) noexcept
{
    if (state.collapsedHostSections.size() > maximumCollapsedHostSections
        || state.terminalWorkspaces.size() > maximumTerminalWorkspaces
        || !std::ranges::all_of(state.profiles, validProfileWorkspaceState)
        || !std::ranges::all_of(state.terminalWorkspaces, validTerminalWorkspaceLayout))
    {
        return false;
    }
    if (!std::ranges::all_of(state.collapsedHostSections, [](const std::string_view section) {
            return validBoundedText(section, 512, false);
        }))
    {
        return false;
    }
    std::size_t restoreIntentCount = 0;
    for (auto workspace = state.terminalWorkspaces.begin(); workspace != state.terminalWorkspaces.end(); ++workspace)
    {
        restoreIntentCount += workspace->restoreIntents.size();
        if (restoreIntentCount > maximumRestorableTerminalSessions
            || std::ranges::find(std::next(workspace), state.terminalWorkspaces.end(), workspace->id,
                                 &TerminalWorkspaceLayout::id)
                   != state.terminalWorkspaces.end())
        {
            return false;
        }
    }
    if (state.terminalWorkspaces.empty())
    {
        return state.activeTerminalWorkspaceId.empty();
    }
    return std::ranges::find(state.terminalWorkspaces, state.activeTerminalWorkspaceId, &TerminalWorkspaceLayout::id)
           != state.terminalWorkspaces.end();
}

bool validTerminalWorkspaceLayout(const TerminalWorkspaceLayout &layout) noexcept
{
    if (!validBoundedText(layout.id, 128, false) || !validBoundedText(layout.title, 256, true)
        || !validBoundedText(layout.rootNodeId, 128, false) || !validBoundedText(layout.activePaneId, 128, false)
        || layout.nodes.empty() || layout.nodes.size() > maximumTerminalNodesPerWorkspace
        || layout.restoreIntents.empty() || layout.restoreIntents.size() > maximumTerminalPanesPerWorkspace
        || !uniqueTerminalIds(layout) || !std::ranges::all_of(layout.restoreIntents, validRestoreIntent))
    {
        return false;
    }

    const TerminalLayoutNode *root = findTerminalNode(layout, layout.rootNodeId);
    if (root == nullptr)
    {
        return false;
    }
    std::array<const TerminalLayoutNode *, maximumTerminalNodesPerWorkspace> pending{};
    std::array<const TerminalLayoutNode *, maximumTerminalNodesPerWorkspace> visited{};
    std::array<const TerminalRestoreIntent *, maximumTerminalPanesPerWorkspace> referencedIntents{};
    std::size_t pendingCount = 1;
    std::size_t visitedCount = 0;
    std::size_t referencedCount = 0;
    pending[0] = root;
    while (pendingCount != 0)
    {
        const TerminalLayoutNode *node = pending[--pendingCount];
        if (std::ranges::find(visited.begin(), visited.begin() + static_cast<std::ptrdiff_t>(visitedCount), node)
            != visited.begin() + static_cast<std::ptrdiff_t>(visitedCount))
        {
            return false;
        }
        visited[visitedCount++] = node;
        if (!validBoundedText(node->id, 128, false))
        {
            return false;
        }
        if (node->kind == TerminalLayoutNodeKind::Leaf)
        {
            if (!node->firstChildId.empty() || !node->secondChildId.empty()
                || !validBoundedText(node->restoreIntentId, 128, false))
            {
                return false;
            }
            const TerminalRestoreIntent *intent = findRestoreIntent(layout, node->restoreIntentId);
            if (intent == nullptr
                || std::ranges::find(referencedIntents.begin(),
                                     referencedIntents.begin() + static_cast<std::ptrdiff_t>(referencedCount), intent)
                       != referencedIntents.begin() + static_cast<std::ptrdiff_t>(referencedCount))
            {
                return false;
            }
            referencedIntents[referencedCount++] = intent;
            continue;
        }
        if (!node->restoreIntentId.empty() || node->firstChildId == node->secondChildId || !std::isfinite(node->ratio)
            || node->ratio < minimumTerminalSplitRatio || node->ratio > maximumTerminalSplitRatio)
        {
            return false;
        }
        const TerminalLayoutNode *first = findTerminalNode(layout, node->firstChildId);
        const TerminalLayoutNode *second = findTerminalNode(layout, node->secondChildId);
        if (first == nullptr || second == nullptr || pendingCount + 2 > pending.size())
        {
            return false;
        }
        pending[pendingCount++] = second;
        pending[pendingCount++] = first;
    }
    const TerminalLayoutNode *active = findTerminalNode(layout, layout.activePaneId);
    return visitedCount == layout.nodes.size() && referencedCount == layout.restoreIntents.size() && active != nullptr
           && active->kind == TerminalLayoutNodeKind::Leaf;
}

TerminalWorkspaceLayout makeSinglePaneTerminalWorkspace(std::string workspaceId, std::string paneId,
                                                        TerminalRestoreIntent intent)
{
    TerminalWorkspaceLayout layout{
        .id = std::move(workspaceId),
        .title = intent.title,
        .rootNodeId = paneId,
        .activePaneId = paneId,
    };
    layout.nodes.push_back(TerminalLayoutNode{.id = std::move(paneId), .restoreIntentId = intent.id});
    layout.restoreIntents.push_back(std::move(intent));
    return layout;
}

bool splitTerminalPane(TerminalWorkspaceLayout &layout, const std::string_view paneId, std::string splitNodeId,
                       std::string newPaneId, TerminalRestoreIntent newIntent,
                       const TerminalSplitOrientation orientation, const double ratio, const bool placeAfter)
{
    if (!validTerminalWorkspaceLayout(layout) || layout.restoreIntents.size() >= maximumTerminalPanesPerWorkspace
        || !validBoundedText(splitNodeId, 128, false) || !validBoundedText(newPaneId, 128, false)
        || !validRestoreIntent(newIntent) || !std::isfinite(ratio) || ratio < minimumTerminalSplitRatio
        || ratio > maximumTerminalSplitRatio || findTerminalNode(layout, splitNodeId) != nullptr
        || findTerminalNode(layout, newPaneId) != nullptr || findRestoreIntent(layout, newIntent.id) != nullptr)
    {
        return false;
    }
    TerminalWorkspaceLayout candidate = layout;
    TerminalLayoutNode *pane = findTerminalNode(candidate, paneId);
    if (pane == nullptr || pane->kind != TerminalLayoutNodeKind::Leaf)
    {
        return false;
    }
    TerminalLayoutNode *parent = findParentNode(candidate, paneId);
    const std::string parentId = parent == nullptr ? std::string{} : parent->id;
    const bool wasFirstChild = parent != nullptr && parent->firstChildId == paneId;
    const std::string originalPaneId(paneId);
    candidate.nodes.push_back(TerminalLayoutNode{.id = newPaneId, .restoreIntentId = newIntent.id});
    candidate.restoreIntents.push_back(std::move(newIntent));
    candidate.nodes.push_back(TerminalLayoutNode{
        .id = splitNodeId,
        .firstChildId = placeAfter ? originalPaneId : newPaneId,
        .secondChildId = placeAfter ? newPaneId : originalPaneId,
        .kind = TerminalLayoutNodeKind::Split,
        .orientation = orientation,
        .ratio = ratio,
    });
    if (parent == nullptr)
    {
        candidate.rootNodeId = std::move(splitNodeId);
    }
    else
    {
        TerminalLayoutNode *updatedParent = findTerminalNode(candidate, parentId);
        if (updatedParent == nullptr)
        {
            return false;
        }
        if (wasFirstChild)
        {
            updatedParent->firstChildId = std::move(splitNodeId);
        }
        else
        {
            updatedParent->secondChildId = std::move(splitNodeId);
        }
    }
    candidate.activePaneId = std::move(newPaneId);
    if (!validTerminalWorkspaceLayout(candidate))
    {
        return false;
    }
    layout = std::move(candidate);
    return true;
}

bool closeTerminalPane(TerminalWorkspaceLayout &layout, const std::string_view paneId)
{
    if (!validTerminalWorkspaceLayout(layout) || layout.restoreIntents.size() <= 1)
    {
        return false;
    }
    TerminalWorkspaceLayout candidate = layout;
    TerminalLayoutNode *pane = findTerminalNode(candidate, paneId);
    TerminalLayoutNode *parent = findParentNode(candidate, paneId);
    if (pane == nullptr || pane->kind != TerminalLayoutNodeKind::Leaf || parent == nullptr)
    {
        return false;
    }
    const std::string intentId = pane->restoreIntentId;
    const std::string parentId = parent->id;
    const std::string siblingId = parent->firstChildId == paneId ? parent->secondChildId : parent->firstChildId;
    TerminalLayoutNode *grandparent = findParentNode(candidate, parentId);
    if (grandparent == nullptr)
    {
        candidate.rootNodeId = siblingId;
    }
    else if (grandparent->firstChildId == parentId)
    {
        grandparent->firstChildId = siblingId;
    }
    else
    {
        grandparent->secondChildId = siblingId;
    }
    std::erase_if(candidate.nodes, [paneId, &parentId](const TerminalLayoutNode &node) {
        return node.id == paneId || node.id == parentId;
    });
    std::erase_if(candidate.restoreIntents, [&intentId](const TerminalRestoreIntent &intent) {
        return intent.id == intentId;
    });
    if (candidate.activePaneId == paneId)
    {
        const TerminalLayoutNode *nextActive = findTerminalNode(candidate, siblingId);
        while (nextActive != nullptr && nextActive->kind == TerminalLayoutNodeKind::Split)
        {
            nextActive = findTerminalNode(candidate, nextActive->firstChildId);
        }
        if (nextActive == nullptr)
        {
            return false;
        }
        candidate.activePaneId = nextActive->id;
    }
    if (!validTerminalWorkspaceLayout(candidate))
    {
        return false;
    }
    layout = std::move(candidate);
    return true;
}

bool resizeTerminalSplit(TerminalWorkspaceLayout &layout, const std::string_view splitNodeId, const double ratio)
{
    if (!std::isfinite(ratio) || ratio < minimumTerminalSplitRatio || ratio > maximumTerminalSplitRatio)
    {
        return false;
    }
    TerminalWorkspaceLayout candidate = layout;
    TerminalLayoutNode *split = findTerminalNode(candidate, splitNodeId);
    if (split == nullptr || split->kind != TerminalLayoutNodeKind::Split)
    {
        return false;
    }
    split->ratio = ratio;
    if (!validTerminalWorkspaceLayout(candidate))
    {
        return false;
    }
    layout = std::move(candidate);
    return true;
}

bool swapTerminalPanes(TerminalWorkspaceLayout &layout, const std::string_view firstPaneId,
                       const std::string_view secondPaneId)
{
    if (firstPaneId == secondPaneId)
    {
        return false;
    }
    TerminalWorkspaceLayout candidate = layout;
    TerminalLayoutNode *first = findTerminalNode(candidate, firstPaneId);
    TerminalLayoutNode *second = findTerminalNode(candidate, secondPaneId);
    if (first == nullptr || second == nullptr || first->kind != TerminalLayoutNodeKind::Leaf
        || second->kind != TerminalLayoutNodeKind::Leaf)
    {
        return false;
    }
    std::swap(first->restoreIntentId, second->restoreIntentId);
    if (!validTerminalWorkspaceLayout(candidate))
    {
        return false;
    }
    layout = std::move(candidate);
    return true;
}

std::vector<std::string> terminalPaneOrder(const TerminalWorkspaceLayout &layout)
{
    std::vector<std::string> order;
    if (!validTerminalWorkspaceLayout(layout))
    {
        return order;
    }
    order.reserve(layout.restoreIntents.size());
    std::array<const TerminalLayoutNode *, maximumTerminalNodesPerWorkspace> pending{};
    std::size_t pendingCount = 1;
    pending[0] = findTerminalNode(layout, layout.rootNodeId);
    while (pendingCount != 0)
    {
        const TerminalLayoutNode *node = pending[--pendingCount];
        if (node->kind == TerminalLayoutNodeKind::Leaf)
        {
            order.push_back(node->id);
            continue;
        }
        pending[pendingCount++] = findTerminalNode(layout, node->secondChildId);
        pending[pendingCount++] = findTerminalNode(layout, node->firstChildId);
    }
    return order;
}

ProfileWorkspaceState *findProfileWorkspaceState(WorkspaceState &state, const std::string_view profileId)
{
    const auto found = std::ranges::find(state.profiles, profileId, &ProfileWorkspaceState::profileId);
    return found == state.profiles.end() ? nullptr : &*found;
}

const ProfileWorkspaceState *findProfileWorkspaceState(const WorkspaceState &state, const std::string_view profileId)
{
    const auto found = std::ranges::find(state.profiles, profileId, &ProfileWorkspaceState::profileId);
    return found == state.profiles.end() ? nullptr : &*found;
}

ProfileWorkspaceState &ensureProfileWorkspaceState(WorkspaceState &state, std::string profileId)
{
    if (ProfileWorkspaceState *existing = findProfileWorkspaceState(state, profileId))
    {
        return *existing;
    }
    state.profiles.push_back(ProfileWorkspaceState{.profileId = std::move(profileId)});
    return state.profiles.back();
}

void recordRecentRemotePath(ProfileWorkspaceState &state, std::string remotePath)
{
    if (!validRemotePath(remotePath))
    {
        return;
    }
    state.lastRemotePath = remotePath;
    std::erase(state.recentRemotePaths, remotePath);
    state.recentRemotePaths.insert(state.recentRemotePaths.begin(), std::move(remotePath));
    if (state.recentRemotePaths.size() > maximumRecentRemotePaths)
    {
        state.recentRemotePaths.resize(maximumRecentRemotePaths);
    }
}

bool remotePathBookmarked(const ProfileWorkspaceState &state, const std::string_view remotePath) noexcept
{
    return std::ranges::find(state.bookmarkedRemotePaths, remotePath) != state.bookmarkedRemotePaths.end();
}

bool toggleBookmarkedRemotePath(ProfileWorkspaceState &state, std::string remotePath)
{
    if (!validRemotePath(remotePath))
    {
        return false;
    }
    const auto found = std::ranges::find(state.bookmarkedRemotePaths, remotePath);
    if (found != state.bookmarkedRemotePaths.end())
    {
        state.bookmarkedRemotePaths.erase(found);
        return false;
    }
    state.bookmarkedRemotePaths.insert(state.bookmarkedRemotePaths.begin(), std::move(remotePath));
    if (state.bookmarkedRemotePaths.size() > maximumBookmarkedRemotePaths)
    {
        state.bookmarkedRemotePaths.resize(maximumBookmarkedRemotePaths);
    }
    return true;
}

} // namespace ztermy::workbench
