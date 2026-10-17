#pragma once

#include "ztermy_version.h"

#include <array>
#include <string_view>

namespace ztermy::terminal
{
inline constexpr std::string_view programName = "ztermy";
inline constexpr std::string_view programVersion = ZTERMY_VERSION_STRING;
inline constexpr std::string_view versionReport = "ztermy(" ZTERMY_VERSION_STRING ")";

struct IdentityEnvironmentEntry final
{
    std::string_view name;
    std::string_view value;
};

inline constexpr std::array identityEnvironment{
    IdentityEnvironmentEntry{.name = "TERM_PROGRAM", .value = programName},
    IdentityEnvironmentEntry{.name = "TERM_PROGRAM_VERSION", .value = programVersion},
    IdentityEnvironmentEntry{.name = "COLORTERM", .value = "truecolor"},
};

// These describe the parent terminal, not a child attached to our ConPTY.
inline constexpr std::array<std::string_view, 18> foreignTerminalMarkers{
    "WT_SESSION",          "WT_PROFILE_ID",   "WT_WINDOWID",           "KITTY_WINDOW_ID",      "KITTY_PID",
    "WEZTERM_PANE",        "WEZTERM_VERSION", "WEZTERM_EXECUTABLE",    "ALACRITTY_SOCKET",     "ALACRITTY_LOG",
    "ALACRITTY_WINDOW_ID", "VTE_VERSION",     "KONSOLE_VERSION",       "KONSOLE_DBUS_SESSION", "GNOME_TERMINAL_SCREEN",
    "ITERM_SESSION_ID",    "TERM_SESSION_ID", "GHOSTTY_RESOURCES_DIR",
};
} // namespace ztermy::terminal
