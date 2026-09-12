# ADR 0115: Optional connection history recording

- Status: Accepted for V5 implementation; runtime acceptance tracked separately
- Date: 2026-09-12

## Decision

`connectionHistoryEnabled` is an application preference introduced by settings
schema 33. Existing schema-32 documents migrate with recording enabled, preserving
previous behavior and all unrelated settings. Unknown future versions remain
read-only failures. Saving general appearance settings must preserve this choice;
resetting application settings restores its default.

The switch lives on the connection history page. Disabling it stops collecting
new connection records and stops automatic updates to existing ones. Connections
themselves, Shell history reading, and explicitly requested raw terminal logging
continue independently. Existing records are neither hidden nor deleted.

Active history entries are finalized at the cutoff time. They retain the existing
`interrupted` status and use the bounded `recording-stopped` phase, displayed as
“Recording stopped”; the timestamp is the end of recording, not a claim that the
terminal disconnected. This uses the existing history storage contract without
changing its schema. Resuming does not backfill unrecorded activity or reopen
finalized records. A later new connection/reconnect can create a new record.

Explicit actions on old records (favorite, delete, clear finished) remain available
and persist while automatic recording is disabled. They never delete raw log files.

## Persistence and failure boundaries

The settings save must succeed before the runtime policy changes. On failure the
switch returns to the stored state and the UI reports the failure. History saves
remain asynchronous and atomic. Only the latest queued snapshot is needed, behind
any in-flight save. Destruction drains that last snapshot rather than dropping it.
Disabling can therefore finish writes of data collected before the cutoff; it does
not synchronously wait for disk I/O on the UI thread.

No terminal input, key material, or new content-capture mechanism is introduced.
Tests cover migration, the cutoff, ignored events, no backfill, independent active
sessions, restart persistence, explicit deletion while disabled, and final flushing.
