# ADR 0107: Pace latest terminal snapshot delivery

- Status: Accepted
- Date: 2026-08-23

## Context

Terminal worker threads already replace an undelivered snapshot with the newest snapshot. The GUI-thread delivery loop,
however, immediately queued another delivery whenever more output arrived. During the 20,000-line Release workload this
caused a median of 1,647 `TerminalItem::setSnapshot` calls in about 1.7 seconds, even though the scene graph rendered a
median of 250 terminal frames. Every delivery also updates input-method and scrollbar state.

## Decision

- Deliver at most one latest snapshot per 8 ms while output is continuous.
- Continue replacing the pending snapshot, so the next delivery always represents the newest terminal state.
- Use a single-shot `QTimer` on the session object's GUI thread. Worker threads only replace pending state and queue timer
  scheduling.
- Apply the same cadence to local and SSH terminal sessions.
- Keep input transport, terminal parsing, command/search processing, and final terminal state independent of this visual
  delivery cadence.

Eight milliseconds supports a responsive path above 120 updates per second while preventing an unconstrained queued-signal
loop on fast producers.

## Evidence

Five warm static Release runs were captured for both paths in the same Direct3D 11 environment. Median results:

| Metric | Immediate delivery | 8 ms latest delivery | Change |
|---|---:|---:|---:|
| Completion | 1,728 ms | 1,729 ms | +0.1% |
| Paint P95 | 4,000 us | 4,000 us | 0% |
| Paint maximum | 7,701 us | 7,356 us | -4.5% |
| GUI snapshot updates | 1,647 | 159 | -90.3% |
| Rendered terminal frames | 250 | 162 | -35.2% |
| Estimated texture upload | 752,572,864 bytes | 476,669,312 bytes | -36.7% |
| Maximum heartbeat gap | 13 ms | 14 ms | +1 ms |

The one-millisecond heartbeat difference is within run-to-run spread (baseline range 12-17 ms, candidate range 12-17 ms)
and remains well below the provisional 33 ms P95 investigation budget.

## Consequences

- Heavy output no longer floods the GUI with snapshots it cannot display.
- Terminal content may be visually delayed by at most one cadence interval during continuous output.
- Snapshot construction still occurs on the producer thread for every read. Reducing producer-side construction is a
  separate optimization and requires a design that cannot lose the final state after a burst.
