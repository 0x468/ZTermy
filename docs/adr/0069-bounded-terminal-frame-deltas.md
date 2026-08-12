# ADR 0069: Bounded terminal frame deltas

Status: accepted

## Context

Command blocks are the preferred evidence for line-oriented shell work, but
REPLs, pagers, debuggers, and alternate-screen applications mutate a visible
terminal frame without producing durable command boundaries. Repeatedly copying
complete terminal snapshots into AI history would be unbounded, while deriving
frames only from raw PTY bytes would duplicate the terminal emulator and lose
the renderer's authoritative cursor and geometry.

## Decision

- Attach one thread-safe `AiTerminalFrameTracker` to each terminal generation.
  Feed it the emulator's rendered snapshots and only the bounded escape tail
  needed to observe DEC private alternate-screen transitions.
- Retain at most 300 rendered lines and 4,096 UTF-8 bytes per line. Keep the
  current frame and the changed-line indexes for only the most recent revision;
  do not append frame snapshots to conversation history.
- Return a full frame for an initial read, an expired revision cursor, or a
  geometry change. Return a one-revision changed-line delta otherwise. Distinguish
  an expired cursor from a geometry-forced full refresh.
- Include revision, base revision, UTC change time, monotonic idle duration,
  dimensions, cursor position/visibility, and alternate-screen state with every
  result. Escape-sequence observation may update metadata but never claims to
  reconstruct terminal cells.
- Keep observation free of persistence, provider calls, Qt UI objects, and
  blocking I/O. The tracker is a domain component and may be read from the AI
  orchestration thread while snapshots arrive from session callbacks.

## Consequences

- Interactive tools can wait on deterministic frame revisions and idle periods
  without polling or persisting unbounded screenshots.
- A consumer that misses more than one revision receives an explicit expired
  cursor and a bounded full refresh, so coalescing is visible rather than silent.
- Geometry changes cannot be misapplied as line deltas against an incompatible
  frame.
- Alternate-screen detection remains advisory. Rendering truth still comes from
  the terminal engine, and later capability adapters must expose degraded or
  uncertain integration states to the user.
