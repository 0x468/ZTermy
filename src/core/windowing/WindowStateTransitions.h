#pragma once

#include <Qt>

namespace ztermy::windowing
{
// Pure transition rules for top-level window states.
//
// Each rule owns exactly one flag and preserves every other flag, so a minimize
// never forgets that the window was maximized and a restore never turns a
// maximized window into a normal one. QWindow::show*() helpers replace the
// whole flag set and therefore must not be used for these transitions.
[[nodiscard]] Qt::WindowStates minimizedStates(Qt::WindowStates current) noexcept;
[[nodiscard]] Qt::WindowStates presentedStates(Qt::WindowStates current) noexcept;
[[nodiscard]] Qt::WindowStates maximizeToggledStates(Qt::WindowStates current) noexcept;
} // namespace ztermy::windowing
