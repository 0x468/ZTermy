# ADR 0070: Live terminal frame tools

Status: accepted

## Context

The immutable per-turn read snapshot prevents accidental retargeting, but it
cannot show progress inside a REPL, pager, debugger, or alternate-screen
application after a turn begins. Sleep-only polling is nondeterministic and
wastes tool calls.

## Decision

- Publish `read_terminal_frame` and `wait_terminal_frame` as separate live tools.
  Both require an exact session identifier, session generation, and frame
  revision. Their target must also exist in the turn's immutable workspace
  target set.
- `read_terminal_frame` returns the tracker's bounded full frame or one-revision
  delta immediately. `wait_terminal_frame` waits for either a newer revision or
  a configured idle threshold, with a bounded timeout of 120 seconds.
- A wait is cancellable independently of the terminal process. It observes at a
  50 ms cadence on the application event loop and performs no provider or
  persistence work while waiting.
- Revalidate the exact generation throughout a wait. Closing or reconnecting the
  target fails explicitly; changing the selected tab cannot retarget the call.
- Include the current dimensions, cursor, alternate-screen state, frame cursor
  status, untrusted-evidence marker, and coarse control owner in every result.
  Do not disclose another conversation identifier.
- Charge reads and waits to the existing per-turn call/repeated-read/time budget.

## Consequences

- Agents can react to observable terminal changes and stable idle states without
  arbitrary sleeps or unbounded snapshots.
- Full-screen state remains evidence rather than authority, and output shown by
  the terminal can still contain prompt injection.
- Wait cancellation never injects Ctrl+C and never changes PTY ownership.
- Shell capability adapters and explicit pause/resume UI remain subsequent 0.3.3
  work; this decision establishes their live observation primitive.
