# ADR 0087: Ingest explicit local text attachments asynchronously

Status: accepted

Date: 2026-08-15

## Context

The AI sidebar already accepts terminal selections and recent semantic command
blocks as explicit context. Users also need to attach configuration fragments,
logs, notes, and source files without copying them through the terminal or
silently granting the Agent ambient filesystem access.

Reading user-selected files on the Qt Quick thread would make the sidebar
unresponsive. Unbounded or binary input would also bypass the context broker's
evidence limits and make provider requests unpredictable.

## Decision

- The attachment menu exposes a native multi-file picker for local text files.
- Selection is explicit. ztermy does not crawl directories, infer related
  files, or automatically import terminal working-directory contents.
- The first implementation accepts regular UTF-8 files without NUL bytes. A
  picker request contains at most four files, and each source file is limited
  to 256 KiB before context processing.
- File loading and validation run on the shared worker pool. Completion is
  delivered to the tab that initiated the request, even if focus changes; a
  closed tab simply discards the result.
- A canonical-path digest identifies an attachment. Reattaching the same path
  refreshes its content instead of creating duplicate chips.
- The existing context broker remains authoritative for per-item and aggregate
  prompt budgets, redaction, pinning, removal, preview, and truncation metadata.
- Rejected files are reported in the AI sidebar while valid files from the same
  selection remain attached.

## Consequences

- Explicit file context follows the same inspectable evidence model as terminal
  selections and command blocks.
- Large, binary, inaccessible, or invalid UTF-8 files do not enter a provider
  request.
- The UI remains responsive while files are read.
- Provider-capable image attachments, reusable skills, and web search remain
  separate follow-up slices of the 0.3.10 program.

