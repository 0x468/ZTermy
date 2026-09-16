#pragma once

class QWindow;

namespace ztermy::windowing
{
// The single sanctioned way to minimize, maximize/restore, or bring back a
// top-level window. Every caller (title-bar buttons, detached windows, tray,
// single-instance activation, workspace transfers) routes through here so the
// maximized state survives minimize, hide, and activation round trips.
//
// Direct QWindow::show(), showNormal(), showMinimized(), showMaximized(), or
// setWindowStates() calls outside this module are rejected by the code
// structure gate.
void minimize(QWindow &window);
void toggleMaximize(QWindow &window);
void reveal(QWindow &window);
void present(QWindow &window);
} // namespace ztermy::windowing
