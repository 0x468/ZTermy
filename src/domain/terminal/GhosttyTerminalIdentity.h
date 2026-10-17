#pragma once

#include "core/terminal/TerminalIdentity.h"

#include <ghostty/vt.h>

#include <cstdint>

namespace ztermy::terminal::detail
{
inline GhosttyResult installTerminalIdentity(const GhosttyTerminal terminal)
{
    const auto callback = +[](GhosttyTerminal, void *) -> GhosttyString {
        return {.ptr = reinterpret_cast<const std::uint8_t *>(versionReport.data()), .len = versionReport.size()};
    };
    return ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_XTVERSION, reinterpret_cast<const void *>(callback));
}
} // namespace ztermy::terminal::detail
