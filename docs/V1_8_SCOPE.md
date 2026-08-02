# V1.8 search, logging, scripts, and observability scope

## Delivered contract

V1.8 hardens long-running professional workflows around explicit data
boundaries:

- opt-in local and SSH session output logging with a non-blocking bounded writer,
  deterministic stop/flush, visible failure state, and dropped-byte warning;
- versioned script-library JSON import/export with validation, atomic merge,
  identifier collision handling, and a 1000-entry bound;
- centralized actions for session logging and script-library interchange;
- existing asynchronous shell-history loading, bounded current/global search,
  remote-file filtering, terminal search, and predictable script run/insert
  behavior retained behind the same workbench surface;
- synthetic tests for writer backpressure, terminal input/output paths, transfer
  queue limits, cancellation, and persistence failures;
- sanitized SSH/SFTP/transfer error codes and recovery state that never include
  credentials or terminal input;
- atomically persisted interrupted-transfer metadata, explicit retry after
  restart, and deterministic stale upload-part cleanup without silent reconnect.

## Data and privacy boundary

Session logging is off by default. ztermy never adds input events to a transcript,
but a remote or local shell may echo typed commands into raw output. The chosen
log file must therefore be handled as sensitive user data. Script libraries and
recovery metadata never contain passwords, passphrases, private keys, terminal
content, or credential-vault payloads. Recovery metadata does contain the local
and remote paths required for an explicit retry and is therefore treated as
sensitive application data rather than telemetry.

## Deliberate boundaries

- Session logs are raw output streams, not rendered HTML, replay recordings, or
  automatically redacted transcripts.
- Import does not execute scripts and does not infer content-based duplicates.
- Periodic remote telemetry is not injected into the interactive shell. A future
  telemetry feature requires an independently authenticated channel and its own
  load/privacy contract.
- AI assistance remains outside V1.8.

## Verification boundary

Automated gates cover storage, validation, failure, bounded-queue, action, and
controller behavior. File dialogs, visual warnings, keyboard flow, raw output
fidelity in real shells, and sustained real-host/storage behavior remain in the
deferred manual acceptance ledger.
