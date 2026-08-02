# ADR 0024: Terminal workbench and shell-history providers

Status: accepted for V1.4

## Context

V1.4 adds shell history and reusable commands to a live terminal. Reading
history, retaining commands, and sending a command followed by Enter are not
presentation-only concerns: they cross SSH threading, persistence, privacy,
focus, and terminal-input boundaries. A direct QML implementation or raw input
recorder would couple these concerns and make later shell integration unsafe.

NetCatty demonstrates a useful product layout and a pragmatic remote-history
reader. Windows Terminal demonstrates that reliable command semantics require
shell-provided marks. ztermy needs useful file-backed history now without
pretending it is equivalent to a semantic command log.

## Decision

- A terminal workbench is a per-tab application state machine. QML renders the
  toolbar, split layout, pages, focus route, and animation. C++ owns page data,
  commands, validation, persistence, session requests, and error states.
- `ShellHistoryProvider` is a protocol/platform-independent application
  boundary. Results carry a shell kind, entries, source, freshness, and typed
  failure. Providers never expose raw transport output to QML.
- Remote POSIX history uses a generic auxiliary SSH command channel on the
  existing authenticated libssh2 session. The channel is opened, pumped, and
  closed only on the session worker. Its nonblocking state machine is
  interleaved with terminal input/output; it has cancellation, deadline, and
  byte limits and cannot stall the interactive channel.
- The initial remote adapter detects the account's Bash, Zsh, or Fish shell and
  reads only bounded default history files. It does not change shell options,
  source startup files, invoke an interactive shell, or modify the host.
- The initial local adapter reads the documented default PSReadLine history
  file asynchronously. It never sends a discovery command through ConPTY.
- Parsed history is capped, sanitized, deduplicated, cached per live tab, and
  erased on tab close. History text, auxiliary command text/output, and terminal
  input are excluded from logs.
- A separate `QuickCommandStore` persists only explicit user saves in a
  versioned JSON document under the resolved application data root. These
  records are presented as code snippets inside Scripts. They are global, not
  per SSH profile, and not stored in the credential vault.
- Insert and Run both target the currently active tab through one controller
  action. Insert uses the terminal's paste encoding without adding Enter. Run
  sends the exact validated UTF-8 command and one carriage return. These are
  deliberate professional-tool actions and do not add a product confirmation
  dialog; the separate multiline clipboard-paste confirmation is unchanged.
- The controller may capture commands submitted through ztermy only when the
  input stream is unambiguous. Editing/navigation control sequences invalidate
  that pending capture. Captured commands are bounded, held per live tab, and
  aggregated for the global history scope without disk persistence.
- Panel page, side, width, Composer visibility, and Composer height belong to
  each live terminal tab and survive tab switching. New tabs start closed so
  the workbench never consumes terminal space implicitly. These transient
  layout values are not profile appearance settings and are not restored after
  process restart in V1.4.

## Consequences

- History can be stale when a shell has not flushed its file, and a nested shell
  can differ from the account default. The UI must disclose the detected source
  and provide Refresh instead of claiming a complete execution log.
- Reusing one SSH connection avoids retaining or re-reading credentials. It
  requires a generic multiplexed auxiliary-channel primitive rather than a
  blocking convenience function, but that primitive can later serve remote
  metadata and SFTP-adjacent operations.
- Best-effort capture is intentionally not called a semantic execution log.
  Full-screen programs, nested shells, IME and complex line editing still need
  OSC 133 integration before ztermy can attach reliable exit status, working
  directory, or command-block metadata.
- Code snippets may contain sensitive text because the user controls them.
  Validation, clear UI copy, restrictive file permissions where supported, and
  the repository's no-secret-logging rule apply; credential-vault coupling does
  not.
- Windows OpenSSH PowerShell and additional shells can be added as providers
  without changing QML or the underlying snippet store.
