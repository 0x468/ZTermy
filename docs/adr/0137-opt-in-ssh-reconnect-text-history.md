# ADR 0137: Opt-in in-memory text history across SSH reconnects

- Status: Accepted
- Date: 2026-10-03

## Decision

`retainHistoryOnReconnect` defaults to false. Application settings schema 44
adds it; schema-43 documents keep all unrelated preferences and default it off.
The ordinary settings draft/apply/discard workflow controls the switch.

When enabled, manual and automatic SSH reconnects copy the old engine's text
into a fresh engine on the SSH worker. No raw escape-stream replay, modes,
clipboard writes, terminal replies, images or styling cross the connection
boundary. Control characters other than newlines and tabs are removed. A
localized new-connection separator identifies each attempt, including failed
attempts. Prior text moves into scrollback and the new active screen is blank.

The transient replay is capped at the most recent 4 MiB of text; the existing
terminal engine scrollback budget still limits final history. No additional
disk persistence or logging occurs. Explicit pane close releases all history.
Local-shell reopen and application restart continue to create empty engines.

If history extraction/replay fails, the old read-only engine remains available
and the reconnect fails visibly instead of silently losing its output.

## Verification

Schema-43 migration covers the off default, opt-in/out round trip, strict
boolean validation and preservation of the old document. Controller tests
exercise persistence. SSH session tests cover off/on across repeated failed
loopback reconnects, copy/search, fresh modes and explicit close cleanup.
Authenticated loopback SSH tests also reconnect twice without destroying the
old view and verify the prior peer's final output after the second connection.
Debug and static Release owning/adjacent gates pass (8 per configuration).
Native Ctrl+R smoke verifies opt-in/out in both main and detached windows and
captures screenshots. No new Shell DLL-init popup events were observed.
