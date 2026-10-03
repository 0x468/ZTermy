# ADR 0136: Retain a read-only terminal after session end

Status: Accepted for read-only interaction; reconnect-history transfer remains pending.
Date: 2026-10-03

## Context

The default session-lifecycle policy already keeps ended panes, but both session
backends rejected view commands once `running` became false. Centered ended and
reconnect panels also covered valuable output. Merely retaining the last rendered
frame was not sufficient for copying, scrolling, searching, or resizing.

## Decision

- Separate live transport/process state from availability of the terminal view.
  Ended views accept scroll, selection, Copy Mode, copy, search, resize, and theme
  changes. They never send terminal input, paste, keys, or mouse reports.
- Keep terminal commands on workers. The local command worker remains available
  after child exit. SSH releases its network resources before waiting for
  read-only commands. Idle workers wait on cancellable conditions, not timers.
- Represent ended/reconnecting state with a dismissible, reserved bottom strip,
  not a centered panel. Only the strip's own bounds shield input. Its geometry
  uses shared Motion enter/exit roles; output remains accessible during dismissal.
- Preserve the existing default `closePaneOnSessionEnd=false` and explicit
  auto-close preference. An explicit pane/tab/application close retires the view,
  joins its workers, and releases the engine and memory.
- Retained output stays in memory only, using the engine's existing bounded
  scrollback. It is not added to workspace/settings files or diagnostic logs.
- Dismissing the status strip does not remove reconnect access. A configurable
  `terminal.reconnect` action defaults to Ctrl+R, uses the existing shortcut
  overrides map (settings schema remains 43), and appears in shortcut settings
  and the command palette. Its native key binding belongs to the focused leaf
  in both main and detached windows, only while an ended saved SSH session is
  eligible. Live shells and auxiliary input fields retain their ordinary keys.
  Reconnecting/connecting sessions reject repeated manual attempts. A preexisting
  customized Ctrl+R binding disables the new default instead of being overwritten.
- On natural local exit, relinquish ConPTY ownership with its release API and
  keep reading through EOF. Do not cancel the pending read or destroy the console
  before its final output is consumed. The pinned redistributable supplies this
  API, independently of the OS API minimum. Explicit close still owns teardown.
  See Microsoft's [ReleasePseudoConsole contract](https://learn.microsoft.com/en-us/windows/console/releasepseudoconsole).
- Format executable paths in Windows-native form in the child command line;
  CMD cannot safely be treated like shells that accept Qt's forward-slash paths.

## Scope and remaining work

This decision does not yet transfer history into a new local child or SSH
connection: restart currently constructs a fresh engine. That is a separate
boundary which must reset parser/input modes, distinguish old and new output,
bound retained memory, and never replay queued input. Do not advertise history
preservation across reconnect, process reopen, or application restart yet.

The security interactions required to establish an SSH connection remain
unchanged. The status strip is not a replacement for host-key confirmation.

## Verification

Owning backend tests cover read-only resize, selection/copy/search, denied input,
and explicit-stop retirement. Terminal item tests cover local selection/scroll
despite retained TUI mouse modes. `retained-terminal-runtime` exercises an actual
isolated CMD exit, native copy shortcut, search, reserved hit regions, dismissal,
and screenshots. Adjacent lifecycle/QML checks remain required.

The native ConPTY test repeats a fast output-and-exit child three times and checks
the last marker and EOF. The runtime fixture also checks full/reduced/off Motion
roles, light/dark surfaces, mixed live/ended panes, and detached-window copying.
These are focused feature gates, not a full release/package acceptance matrix.

On 2026-10-03, the nine owning/adjacent gates passed in both Debug and static
Release, with no new Shell DLL-init popup events. Formatting, QML quality,
translations and affected C++ static analysis passed. The detached runtime
fixture activates and settles the target Pane before selection/copy; it must
not treat a still-rebinding viewport as a stable input target. No clipboard
production workaround was retained. Real SSH disconnect interaction remains
an owner acceptance check, and reconnect-history transfer is not complete.

The reconnect-shortcut follow-up passed twelve owning/adjacent gates in both
Debug and static Release. Native loopback SSH failures verify main/detached
Ctrl+R routing after dismissal, an actual search-field focus exclusion,
rebind/unbind/reset, and screenshots, without authentication or remote commands.
The live isolated CMD key event remains delivered to its backend. The fixture
clears CMD's control-key line-editing effect before its separate output test,
and processes its sentinel clipboard-write notifications before the next copy.
Earlier clipboard-gate failures are retained in local logs; no clipboard
production workaround was introduced. Settings schema 43 is unchanged.
