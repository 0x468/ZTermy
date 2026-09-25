#pragma once

#include <ghostty/vt.h>
#include "domain/terminal/TerminalInput.h"

namespace ztermy::terminal::detail
{
[[nodiscard]] GhosttyKey ghosttyKey(TerminalKey key) noexcept;
[[nodiscard]] GhosttyKeyAction ghosttyKeyAction(TerminalKeyAction action) noexcept;
[[nodiscard]] GhosttyMouseAction ghosttyMouseAction(TerminalMouseAction action) noexcept;
[[nodiscard]] GhosttyMouseButton ghosttyMouseButton(TerminalMouseButton button) noexcept;
} // namespace ztermy::terminal::detail
