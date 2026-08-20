# ADR 0094: Bounded current-terminal command-output artifacts

Status: Accepted

Date: 2026-08-21

## Context

Semantic command blocks originally retained at most 64 KiB as a 16 KiB head
plus a moving tail. That representation is efficient for history previews, but
it cannot recover the evicted middle through `read_command_output`. The AI turn
also captured one command-block snapshot before provider dispatch, so a command
started later in the same turn was absent from that snapshot.

Public terminal products converge on two useful behaviors without requiring
ambient terminal ingestion: Warp exposes terminal Blocks as explicit context
and automatically keeps commands run inside one Agent conversation in that
conversation; Wave gates terminal context explicitly. Netcatty's current source
also separates a bounded model preview from a retrievable tool-output handle.
These are product and architecture observations only; no third-party source is
copied.

## Decision

- Keep `CommandBlockStore`'s 64 KiB head/tail representation as the fast
  semantic-history preview.
- In the same per-terminal store, retain command output as a separate artifact:
  2 MiB per command and 8 MiB total per terminal by default. Both limits are
  configurable domain constants and include only retained payload bytes.
- Capture artifacts on the terminal session worker before queued UI consumers.
  Reads take the existing observer mutex and copy only the requested page, up to
  the AI tool's 16 KiB limit. No artifact payload enters QML or the normal
  semantic snapshot.
- A finished artifact may be evicted oldest-first to admit output for the active
  command. Running artifacts are never evicted to make room for another block.
- Preserve discontinuous observations as bounded stream-offset segments.
  Pagination reports skipped bytes, retained/omitted byte counts, readable
  continuation, observed-stream continuation, artifact completeness, and
  expiration explicitly. It must never present unavailable bytes as complete.
- Bind the live reader to the `SemanticTerminalObserver` captured for the
  owning terminal generation. Tab closure or reconnect generation change
  invalidates the tool call through the existing scope check; no terminal can
  read another terminal's artifact.
- The model may page an artifact created after the AI turn began. It must not
  re-run a command merely to recreate output that remains readable.
- Artifacts are in-memory and session-lifetime only. Persistence or disk spill
  is not implied by this decision.

## Consequences

- Long output up to the artifact limit is recoverable end-to-end even when its
  preview has already collapsed to head/tail.
- Model-visible output is fresher and more accurate without copying multi-MiB
  snapshots on the GUI thread.
- Memory remains predictably bounded per open terminal. Output beyond the
  per-command cap or evicted by the terminal-wide cap is explicitly unavailable
  rather than silently truncated.
- `read_command_block` remains the compact summary API; `read_command_output`
  is the pageable evidence API.

## Public references

- <https://docs.warp.dev/agent-platform/local-agents/agent-context/blocks-as-context>
- <https://docs.warp.dev/agent-platform/capabilities/full-terminal-use>
- <https://docs.waveterm.dev/waveai>
