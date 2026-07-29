# V1 acceptance

Status: draft

Only runtime evidence can mark a platform or UI item complete.

## Current milestone evidence

- MSVC + Ninja dynamic Debug build succeeds.
- MSVC + Ninja static Qt Release build succeeds.
- Native window creation and QML loading succeed on Windows 11.
- Automated hit-test coverage passes for caption, maximize button, client
  area, all resize edges/corners, and maximized-state behavior.
- Custom caption commands, maximize-button Snap Layout hover, and Win+Z were
  manually verified on the primary Windows 11 development machine.

The checkboxes below remain open until the corresponding behavior has been
manually exercised across the required Windows 11 and mixed-DPI scenarios.
Use [testing/WINDOW_SHELL.md](testing/WINDOW_SHELL.md) for the repeatable
window-shell procedure and expected results.

## Window shell

- [x] Custom title bar preserves minimize, maximize, restore, and close
- [x] Hovering maximize/restore shows Windows 11 Snap Layouts
- [x] Win+Z and snap keyboard shortcuts work
- [ ] All edges and corners resize with native cursors
- [x] Double-clicking draggable title space toggles maximize/restore
- [ ] Maximized window respects the monitor work area
- [ ] Moving between mixed-DPI monitors preserves geometry and sharp rendering
- [ ] Dark/light DWM integration is correct
- [ ] Opacity and backdrop settings fail safely when unsupported

## Terminal

- [ ] Local input dispatch P95 is below 16 ms
- [ ] SSH input adds no application-side batching delay
- [ ] Large output does not freeze the window
- [ ] ANSI colors, alternate screen, cursor, clear, and resize are correct
- [ ] CJK, wide characters, combining marks, emoji, and IME are correct
- [ ] Selection, copy, paste, search, and scrollback are stable
- [ ] Thirty minutes of interaction shows no growing latency

## SSH security and reliability

- [ ] Password and key authentication pass on real hosts
- [ ] Unknown host keys require confirmation
- [ ] Changed host keys block the connection
- [ ] Authentication failure, timeout, refusal, and remote close are distinct
- [ ] Twenty connect/disconnect cycles leave no workers or handles behind
- [ ] Logs and configuration contain no credentials

## Distribution

- [ ] Dynamic developer build runs from a clean deployment directory
- [ ] Static release build starts without Qt DLLs
- [ ] Portable data remains inside the portable directory
- [ ] Installed data survives upgrade and uninstall
- [ ] A clean Windows 11 machine runs the release without developer tools
