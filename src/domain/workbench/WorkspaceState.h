#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::workbench
{

struct ProfileWorkspaceState final
{
    std::string profileId;
    std::string lastRemotePath = "/";
    std::vector<std::string> recentRemotePaths;
    std::vector<std::string> bookmarkedRemotePaths;
    std::string workbenchPage = "history";
    std::string workbenchSide = "left";
    double workbenchWidth = 520.0;
    double composerHeight = 132.0;

    [[nodiscard]] friend bool operator==(const ProfileWorkspaceState &, const ProfileWorkspaceState &) = default;
};

struct WorkspaceState final
{
    std::vector<ProfileWorkspaceState> profiles;
    std::vector<std::string> collapsedHostSections;

    [[nodiscard]] friend bool operator==(const WorkspaceState &, const WorkspaceState &) = default;
};

inline constexpr std::size_t maximumRecentRemotePaths = 12;
inline constexpr std::size_t maximumBookmarkedRemotePaths = 32;
inline constexpr std::size_t maximumCollapsedHostSections = 256;

[[nodiscard]] bool validProfileWorkspaceState(const ProfileWorkspaceState &state) noexcept;
[[nodiscard]] bool validWorkspaceState(const WorkspaceState &state) noexcept;
[[nodiscard]] ProfileWorkspaceState *findProfileWorkspaceState(WorkspaceState &state, std::string_view profileId);
[[nodiscard]] const ProfileWorkspaceState *findProfileWorkspaceState(const WorkspaceState &state,
                                                                     std::string_view profileId);
ProfileWorkspaceState &ensureProfileWorkspaceState(WorkspaceState &state, std::string profileId);
void recordRecentRemotePath(ProfileWorkspaceState &state, std::string remotePath);
[[nodiscard]] bool remotePathBookmarked(const ProfileWorkspaceState &state, std::string_view remotePath) noexcept;
[[nodiscard]] bool toggleBookmarkedRemotePath(ProfileWorkspaceState &state, std::string remotePath);

} // namespace ztermy::workbench
