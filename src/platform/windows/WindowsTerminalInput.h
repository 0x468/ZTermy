#pragma once

#include "domain/terminal/TerminalEngine.h"

#include <Qt>

class QKeyEvent;

namespace ztermy::platform::windows
{

[[nodiscard]] terminal::TerminalModifiers terminalModifiers(Qt::KeyboardModifiers modifiers) noexcept;

// Convert Qt's Windows-native key metadata into the physical W3C key code
// expected by libghostty. Text remains layout-dependent and is carried
// separately by TerminalKeyEvent::text.
[[nodiscard]] terminal::TerminalKeyEvent terminalKeyEvent(const QKeyEvent &event, terminal::TerminalKeyAction action,
                                                          bool composing = false);

} // namespace ztermy::platform::windows
