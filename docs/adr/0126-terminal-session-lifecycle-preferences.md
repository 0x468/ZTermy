# ADR 0126: Terminal session lifecycle preferences

- Status: Accepted
- Date: 2026-09-20

## Context

Terminal workspace restoration previously hard-coded three different behaviors:
layouts were always preserved, local shells were always reopened, and SSH
sessions were always left disconnected. A local shell that exited could only be
closed, while SSH end states exposed a separate host-navigation action.

## Decision

- Preserve terminal tabs and pane layouts by default.
- Reopen restored local shells by default; leave restored SSH sessions
  disconnected by default.
- Let users independently disable layout preservation, local reopen, and remote
  reconnect.
- Keep ended panes visible by default with exactly two actions: reopen/reconnect
  and close. An optional policy closes panes only after the session has ended
  and no SSH reconnect is pending.
- Store these choices in application-settings schema 38. Schema 37 documents
  load with the existing defaults.

## Consequences

The defaults remain compatible with prior releases. Disabling preservation
discards only terminal tab and pane layout state on the next launch; unrelated
workspace, SFTP, profile, and credential data remains intact.
