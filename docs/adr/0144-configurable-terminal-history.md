# Configurable terminal history with a bounded page budget

Status: Accepted

Application settings schema 45 adds `terminalHistoryLines`, default 10,000,
range 0–1,000,000; zero disables scrollback. Schema 44 loads with the new
default and preserves all unrelated settings. Unknown future schemas remain
read-only failures. The application version is unchanged.

Every local/SSH pane has the same independent 64 MiB logical terminal-page
budget. The byte and physical-row limits apply simultaneously; whichever is
reached first evicts the oldest history. This is not a guarantee of one million
rows, nor a process-wide RSS cap. Ghostty rounds/prunes at page granularity,
may retain a page beyond the requested row count, and must always hold the
active screen. Images, UI snapshots and allocator pools have separate budgets.

Settings remain drafts until Apply. Existing live and ended views receive the
new limit through their owning worker command queue; new/reconnected sessions
receive it on construction. No terminal command is injected. Shrinking a limit
can discard old history; increasing it cannot recover evicted history.

SSH reconnect replay remains separately limited to the most recent 4 MiB of
plain text. Replayed text still obeys the new engine's history limits.
