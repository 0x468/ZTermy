# ADR 0076: AI concurrency and lifecycle soak gates

Status: accepted

## Context

Short unit tests cover protocol states but can miss retained callbacks, process
handles, late cancellation, stale definitions, and cumulative lifecycle faults.
The release plan also requires repeatable long-duration evidence without making
an eight-hour test part of every developer build.

## Decision

- Add an in-process stress test that repeats 32 MCP server start, discovery,
  exact-schema approval, call, cancellation, periodic restart, and shutdown
  cycles. Every shutdown must remove definitions and no cancelled call may
  complete later.
- Add `ztermy_ai_concurrency_soak`, a provider-independent CMake target that
  repeatedly runs nine streaming, retry, budget, dispatch-ledger, read/fanout,
  frame, MCP process, runtime-manager, and lifecycle-stress tests.
- Make duration a CMake cache value, `ZTERMY_AI_SOAK_SECONDS`, with a two-minute
  developer default and a maximum accepted script duration of 24 hours. Emit a
  versioned JSON report with exact tests, start/completion timestamps, requested
  and actual duration, iterations, failures, and build directory.
- Fail immediately on the first failed iteration. Do not collect prompts,
  terminal content, credentials, provider payloads, or tool arguments in the
  report.
- Keep release-duration evidence separate from the mechanism: the final
  candidate requires the documented two-hour mixed AI run and eight-hour
  AI-disabled/idle terminal run. A two-minute developer run proves the gate
  works but does not impersonate those durations.

## Consequences

- Lifecycle regressions become reproducible locally and in CI without a cloud
  provider or real host.
- The stress test is intentionally slower than ordinary unit tests (about 40
  seconds on the reference machine) and carries `ai;stress` labels.
- Provider/model quality and real-host behavior remain separate acceptance
  evidence; passing this gate proves resource and state discipline, not model
  usefulness.

