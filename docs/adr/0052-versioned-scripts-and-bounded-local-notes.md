# ADR 0052: Versioned scripts and bounded local notes

## Status

Accepted for V2.13.

## Context

ztermy already stores single command snippets and can record commands invoked
through trusted application surfaces. That model cannot express reusable input,
multiple reviewed steps, or deterministic output-gated progression. The
terminal workbench also has no durable local place for operational Markdown
notes. Folding either feature into terminal history, profile persistence, or
the credential vault would blur security and lifecycle boundaries.

## Decision

Use two independent, versioned repositories.

The script repository stores validated domain definitions and migrates the
existing quick-command schema. A per-terminal execution state machine renders
typed non-secret variables, fixes a single explicit target, and advances steps
only immediately or after a bounded literal-output matcher succeeds. Session
output is observed through a fan-out sink: logging keeps its existing contract,
while the matcher receives bounded byte chunks and posts state transitions back
to the application thread. The matcher never parses prompts or retains an
unbounded transcript.

The notes repository uses ordinary UTF-8 Markdown files beneath a dedicated
root. All relative paths are validated and resolved beneath that root before
access. Atomic writes preserve the previous file on failure. Enumeration and
search obey count, size, depth, result, and generation limits and execute away
from GUI and Qt Quick render threads.

Neither repository stores credentials. Script variable substitution is
verbatim and reviewable rather than pretending to provide portable shell
escaping. Notes are explicitly local plaintext content.

## Consequences

- Existing snippets remain recoverable through a deterministic migration while
  the UI can consistently present them as scripts.
- Output-trigger automation is useful but cannot become an unbounded terminal
  scraper or hidden background job.
- Installed and portable notes can be inspected, backed up, and moved without a
  proprietary database, at the cost of not providing at-rest encryption.
- A future scheduler, secret parameter source, regex engine, multi-target fanout,
  cloud sync, or remote notes backend requires a separate ADR and threat model.

