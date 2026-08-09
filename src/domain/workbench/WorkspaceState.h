#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
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
    std::string sftpViewMode = "list";
    std::string sftpSortColumn = "name";
    std::string sftpFilenameEncoding = "utf-8";
    bool followTerminalDirectory = false;
    bool sftpSortAscending = true;
    bool sftpDirectoriesFirst = true;
    bool sftpShowModifiedColumn = true;
    bool sftpShowSizeColumn = true;
    bool sftpShowTypeColumn = false;
    double workbenchWidth = 520.0;
    double composerHeight = 132.0;

    [[nodiscard]] friend bool operator==(const ProfileWorkspaceState &, const ProfileWorkspaceState &) = default;
};

enum class TerminalRestoreKind : std::uint8_t
{
    Local,
    SshProfile,
    Transient,
};

enum class TerminalLayoutNodeKind : std::uint8_t
{
    Leaf,
    Split,
};

enum class TerminalSplitOrientation : std::uint8_t
{
    Horizontal,
    Vertical,
};

struct TerminalRestoreIntent final
{
    std::string id;
    std::string profileId;
    std::string title;
    TerminalRestoreKind kind = TerminalRestoreKind::Local;

    [[nodiscard]] friend bool operator==(const TerminalRestoreIntent &, const TerminalRestoreIntent &) = default;
};

struct TerminalLayoutNode final
{
    std::string id;
    std::string restoreIntentId;
    std::string firstChildId;
    std::string secondChildId;
    TerminalLayoutNodeKind kind = TerminalLayoutNodeKind::Leaf;
    TerminalSplitOrientation orientation = TerminalSplitOrientation::Horizontal;
    double ratio = 0.5;

    [[nodiscard]] friend bool operator==(const TerminalLayoutNode &, const TerminalLayoutNode &) = default;
};

struct TerminalWorkspaceLayout final
{
    std::string id;
    std::string title;
    std::string rootNodeId;
    std::string activePaneId;
    std::vector<TerminalLayoutNode> nodes;
    std::vector<TerminalRestoreIntent> restoreIntents;

    [[nodiscard]] friend bool operator==(const TerminalWorkspaceLayout &, const TerminalWorkspaceLayout &) = default;
};

struct WorkspaceState final
{
    std::vector<ProfileWorkspaceState> profiles;
    std::vector<std::string> collapsedHostSections;
    std::vector<TerminalWorkspaceLayout> terminalWorkspaces;
    std::string activeTerminalWorkspaceId;

    [[nodiscard]] friend bool operator==(const WorkspaceState &, const WorkspaceState &) = default;
};

inline constexpr std::size_t maximumRecentRemotePaths = 12;
inline constexpr std::size_t maximumBookmarkedRemotePaths = 32;
inline constexpr std::size_t maximumCollapsedHostSections = 256;
inline constexpr std::size_t maximumTerminalWorkspaces = 32;
inline constexpr std::size_t maximumTerminalPanesPerWorkspace = 8;
inline constexpr std::size_t maximumTerminalNodesPerWorkspace = 15;
inline constexpr std::size_t maximumRestorableTerminalSessions = 32;
inline constexpr double minimumTerminalSplitRatio = 0.2;
inline constexpr double maximumTerminalSplitRatio = 0.8;

[[nodiscard]] bool validProfileWorkspaceState(const ProfileWorkspaceState &state) noexcept;
[[nodiscard]] bool validWorkspaceState(const WorkspaceState &state) noexcept;
[[nodiscard]] bool validTerminalWorkspaceLayout(const TerminalWorkspaceLayout &layout) noexcept;
[[nodiscard]] TerminalWorkspaceLayout makeSinglePaneTerminalWorkspace(std::string workspaceId, std::string paneId,
                                                                      TerminalRestoreIntent intent);
[[nodiscard]] bool splitTerminalPane(TerminalWorkspaceLayout &layout, std::string_view paneId, std::string splitNodeId,
                                     std::string newPaneId, TerminalRestoreIntent newIntent,
                                     TerminalSplitOrientation orientation, double ratio = 0.5, bool placeAfter = true);
[[nodiscard]] bool closeTerminalPane(TerminalWorkspaceLayout &layout, std::string_view paneId);
[[nodiscard]] bool resizeTerminalSplit(TerminalWorkspaceLayout &layout, std::string_view splitNodeId, double ratio);
[[nodiscard]] bool swapTerminalPanes(TerminalWorkspaceLayout &layout, std::string_view firstPaneId,
                                     std::string_view secondPaneId);
[[nodiscard]] std::vector<std::string> terminalPaneOrder(const TerminalWorkspaceLayout &layout);
[[nodiscard]] ProfileWorkspaceState *findProfileWorkspaceState(WorkspaceState &state, std::string_view profileId);
[[nodiscard]] const ProfileWorkspaceState *findProfileWorkspaceState(const WorkspaceState &state,
                                                                     std::string_view profileId);
ProfileWorkspaceState &ensureProfileWorkspaceState(WorkspaceState &state, std::string profileId);
void recordRecentRemotePath(ProfileWorkspaceState &state, std::string remotePath);
[[nodiscard]] bool remotePathBookmarked(const ProfileWorkspaceState &state, std::string_view remotePath) noexcept;
[[nodiscard]] bool toggleBookmarkedRemotePath(ProfileWorkspaceState &state, std::string remotePath);

} // namespace ztermy::workbench
