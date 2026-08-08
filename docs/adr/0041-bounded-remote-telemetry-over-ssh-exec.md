# ADR 0041: Collect bounded remote telemetry over auxiliary SSH exec

## Status

Accepted for 0.2.6.

## Context

The terminal header needs lightweight remote CPU, memory, disk, network, and
latency feedback. Writing commands into the interactive shell would alter shell
history, prompt state, terminal output, and full-screen applications. Opening a
second authenticated connection per tab would duplicate credentials, host-key
state, sockets, and shutdown work.

The SSH transport already exposes one auxiliary exec channel for remote history.
That channel is isolated from the PTY but can run only one command at a time.

## Decision

Use that auxiliary channel as a single, priority-coordinated background probe
channel:

1. Interactive terminal I/O always proceeds independently.
2. User-requested shell history has priority and cancels/suppresses telemetry.
3. Telemetry runs only for the connected active tab while its terminal page and
   application window are visible.
4. Linux collectors use fixed, read-only commands. No profile/user command is
   interpolated into the probe.
5. The response starts with `ZTERMY_TELEMETRY_V1` and is parsed by Qt-free domain
   code. Counters are converted to percentages/rates from consecutive samples.
6. Fast sampling is five seconds; process/filesystem details are refreshed at a
   slower cadence. Output, duration, item counts, failure retries, and history
   are bounded as recorded in `docs/V2_6_SCOPE.md`.
7. Samples live only in the owning tab and are destroyed on close.

## Consequences

- No terminal pollution and no extra SSH authentication/socket per tab.
- History and telemetry need explicit arbitration, cancellation, and stale-result
  suppression.
- A slow server cannot accumulate unbounded output or background work.
- The initial collector is Linux-only, while the versioned domain protocol and UI
  remain ready for later OS adapters.
- Probe latency describes the auxiliary SSH command round trip; it is not ICMP
  latency and the UI must label it accordingly in details.

## Rejected alternatives

- **Inject commands into the PTY:** corrupts user-visible terminal semantics.
- **Dedicated monitoring SSH connection:** unnecessary connection and credential
  lifetime complexity for a foreground-only convenience feature.
- **Persistent agent on the host:** deployment and trust burden is unsuitable for
  a personal SSH client.
- **Unbounded high-frequency polling:** wastes remote and local resources and can
  interfere with interactive work.
