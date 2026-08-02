# ADR 0028: Explicit bounded session output logging

## Status

Accepted for V1.8.

## Context

Users need an auditable session transcript without allowing file I/O to block
the terminal reader or Qt Quick render thread. A transcript can contain
sensitive remote output and, when a shell echoes input, sensitive commands even
though ztermy does not independently record terminal input.

## Decision

- Logging is off by default and starts only after the user selects a file.
- Local and SSH sessions expose the same byte-oriented output-sink boundary.
- A dedicated writer thread drains a bounded 4 MiB queue and flushes it when
  logging stops or the owning tab closes.
- Queue exhaustion never stalls terminal output. Dropped bytes are counted and
  shown as an incomplete-log warning.
- The selected file is replaced at start. Open and write failures become visible
  session state; they are never hidden in debug-only logs.
- ztermy records raw terminal output bytes before terminal emulation. It never
  adds keystrokes, paste payloads, credentials, passphrases, or private-key
  material through a separate input hook.

## Consequences

Raw output preserves escape sequences and shell fidelity, but is not a
presentation-ready plain-text export. Shell echo can still place typed commands
in the file, so the UI and documentation must treat every transcript as
sensitive user data. A bounded queue protects interactivity at the explicit cost
of a visible incomplete transcript under sustained storage backpressure.

