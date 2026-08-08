#include "domain/workbench/WorkspaceState.h"

#include <algorithm>
#include <cmath>
#include <iterator>
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

} // namespace

bool validProfileWorkspaceState(const ProfileWorkspaceState &state) noexcept
{
    if (state.profileId.empty() || state.profileId.size() > 256 || !validRemotePath(state.lastRemotePath)
        || state.recentRemotePaths.size() > maximumRecentRemotePaths
        || state.bookmarkedRemotePaths.size() > maximumBookmarkedRemotePaths || !validPage(state.workbenchPage)
        || (state.workbenchSide != "left" && state.workbenchSide != "right") || !std::isfinite(state.workbenchWidth)
        || (state.sftpViewMode != "list" && state.sftpViewMode != "tree") || state.workbenchWidth < 320.0
        || state.workbenchWidth > 960.0 || !std::isfinite(state.composerHeight) || state.composerHeight < 96.0
        || state.composerHeight > 480.0)
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
        || !std::ranges::all_of(state.profiles, validProfileWorkspaceState))
    {
        return false;
    }
    return std::ranges::all_of(state.collapsedHostSections, [](const std::string_view section) {
        return !section.empty() && section.size() <= 512 && section.find('\0') == std::string_view::npos;
    });
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
