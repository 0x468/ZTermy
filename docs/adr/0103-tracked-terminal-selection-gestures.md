# ADR 0103: Tracked terminal selection gestures

## Status

Accepted for ztermy 0.4.1.

## Context

`TerminalItem` currently sends start and end rows in visible viewport
coordinates. Local and SSH sessions enqueue those values, and
`GhosttyTerminalEngine::setSelection` resolves both endpoints with
`GHOSTTY_POINT_TAG_VIEWPORT` when the worker processes the command.

This works while the viewport is stationary. It cannot support edge-drag
autoscroll correctly: after the viewport scrolls, the original row number names
a different terminal row. Alternating independent scroll and selection commands
also prevents useful queue coalescing and can publish a snapshot for every timer
tick.

The pinned libghostty revision already exposes `GhosttySelectionGesture`,
tracked grid references, repeat-click behavior, word/line drag granularity, and
an autoscroll-tick event. These operations are explicitly designed to be
serialized with terminal mutations.

## Decision

Selection gesture identity belongs to the terminal engine/worker, not QML and
not visible viewport coordinates.

- Add platform-neutral press, drag, release, cancel, and autoscroll-tick gesture
  requests to the terminal domain boundary.
- `TerminalItem` converts Qt positions, modifiers, click time, cell geometry,
  and edge overshoot into those requests. It does not own a long-lived terminal
  content reference.
- Local and SSH sessions serialize gesture requests with feed, resize, scroll,
  and search operations.
- The Ghostty engine owns one gesture state per terminal, uses the public
  selection gesture API, installs returned snapshot selections immediately, and
  retains the tracked anchor across terminal mutations.
- An autoscroll tick is one compound command. libghostty is the sole owner of
  the scroll-and-extend operation; ztermy never pre-scrolls the viewport and
  then asks the gesture to scroll it again. The worker installs each returned
  tracked selection and publishes at most one snapshot for the compound tick.
- Adjacent pending drag/autoscroll commands may be coalesced by accumulating the
  bounded scroll delta and retaining the newest pointer endpoint. Press,
  release, cancel, copy, resize, and input are ordering barriers.
- Input and paste clear selection and scroll to the live bottom, preserving the
  established ztermy contract.
- Losing the mouse grab, focus, visible item, snapshot, tab, or session cancels
  the UI timer. The worker gesture is released/cancelled so tracked references
  cannot outlive their session.
- `Select all retained scrollback` is a separate engine operation using full
  screen refs. It is not synthesized from viewport rows.

## Autoscroll policy

- Only vertical autoscroll is supported because the terminal has no horizontal
  viewport.
- A short edge dwell prevents accidental activation.
- Speed rises with pointer overshoot and is bounded. Timer cadence and elapsed
  time are separate so delayed GUI ticks cannot create unbounded jumps.
- The UI permits at most one new compound request per timer tick. Session queues
  coalesce pending updates, and the engine publishes one resulting snapshot.
- At retained-history boundaries, the engine reports an unchanged result so
  repeated ticks do not cause continuous snapshot construction or delivery.
- Copy-on-select fires once on gesture completion, never on intermediate ticks.

## Consequences

### Positive

- Selection anchors do not drift while scrolling, output arrives, or snapshots
  coalesce.
- Double-click-drag and triple-click-drag use the same tested engine semantics as
  ordinary selection.
- QML remains presentation glue and does not duplicate terminal grid identity.
- The same stable anchor becomes the basis for 0.4.3 keyboard Copy Mode.

### Cost

- The terminal engine interface and both session command queues gain gesture
  operations.
- Tests need a deterministic terminal-engine path in addition to synthetic Qt
  pointer tests.
- Resize/reflow, retained-history eviction, and session reset must explicitly
  invalidate gestures.

## Rejected alternatives

### Keep viewport coordinates and add a QTimer

Rejected because scrolling changes the meaning of the saved anchor row.

### Convert every endpoint to a screen row in QML

Rejected because QML observes coalesced snapshots and cannot authoritatively
account for pending worker scrolls, output, resize, or reflow.

### Maintain a second selection model outside libghostty

Rejected because it would duplicate tracked grid identity, word/line semantics,
wide-cell normalization, and mutation lifetime rules already exposed by the
pinned engine.
