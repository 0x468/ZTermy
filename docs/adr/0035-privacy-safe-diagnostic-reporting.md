# ADR 0035: Privacy-safe diagnostic reporting

Status: accepted

## Context

Daily use needs enough evidence to identify build, runtime, packaging, logging,
and crash-report availability. Automatically bundling logs or minidumps would
be convenient, but logs can contain local paths or host metadata and minidumps
can contain process memory, including terminal or credential material.

## Decision

- Expose diagnostics through a dedicated application service rather than adding
  platform and file-export responsibilities to `AppController`.
- Export an atomic, versioned JSON document containing application/runtime
  identity, storage-mode, system identity, and aggregate log/crash file
  count/size/recent-time metadata.
- Do not export filenames, absolute paths, logs, dumps, terminal data, command
  history, profiles, credentials, or secret-bearing command lines.
- Provide separate actions to open the log and crash directories. The user must
  inspect and select any sensitive artifact deliberately.
- State in the UI that crash dumps can contain in-memory data and are never
  attached to the JSON report automatically.

## Consequences

- A diagnostic report is safe to inspect and generally safe to share without
  disclosing terminal activity or saved connection data.
- Detailed failures still require the owner to opt into sharing a selected log
  or dump after review.
- The JSON schema begins at version 1 and can evolve without coupling diagnostics
  to the main application controller or to Windows shell UI.
