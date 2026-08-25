#pragma once

#include "domain/terminal/TerminalEngine.h"

#include <cstdint>
#include <string>

namespace ztermy::terminal
{

inline constexpr std::size_t maximumTerminalHyperlinkUriBytes = 4096;
inline constexpr std::size_t maximumTerminalHyperlinksPerSnapshot = 1024;
inline constexpr std::size_t maximumAutomaticLinksPerSnapshot = 256;

[[nodiscard]] std::uint32_t internTerminalHyperlink(TerminalSnapshot &snapshot, std::string uri,
                                                    TerminalHyperlinkKind kind);

// Adds bounded HTTP(S) matches for cells that are not already claimed by an
// explicit OSC 8 hyperlink. This runs when a worker produces a new immutable
// viewport snapshot, never from the scene-graph paint path.
void detectAutomaticTerminalLinks(TerminalSnapshot &snapshot);

} // namespace ztermy::terminal
