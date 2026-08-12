# ADR 0066: Bounded AI SFTP navigation

Status: accepted

## Context

The immutable operations snapshot exposes only the directory currently visible
in the SFTP browser. Diagnosis often needs another absolute directory, but an AI
read must not navigate the user's browser, reuse its request identifier, or turn
a cancelled late result into a UI tree update.

## Decision

- Add `list_sftp_path` with an exact session generation, normalized absolute
  remote path, and a page of at most 100 entries under the 60 KiB result limit.
- Require the session's SFTP browser to be connected; never open a hidden
  credential or host-key flow.
- Route requests through the existing SFTP worker and tree-list operation, but
  reserve the high request-id bit for AI reads. AI results are consumed before
  the browser model and therefore do not mutate its tree or current path.
- Cancellation is logical for an in-flight directory list: ownership is cleared
  immediately and a late high-bit result is discarded. Session/application
  shutdown still interrupts the underlying transport through its session stop
  token.
- Re-check the requested path and reconnect generation at completion. Return
  typed entries and mark every name/path as untrusted evidence.

## Consequences

- The assistant can navigate arbitrary safe absolute directories without
  changing what the user sees in SFTP.
- A cancelled transport call may finish in the worker, but it cannot complete a
  cancelled tool or fall through into the visible tree model.
- Per-operation transport cancellation remains available for file reads; adding
  it to directory enumeration would require an independently cancellable
  libssh2 listing contract.
