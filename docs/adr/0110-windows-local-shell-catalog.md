# ADR 0110: Cached Windows local shell catalog

## Status

Accepted.

## Context

ztermy previously launched `pwsh.exe` unconditionally and only retried with
Windows PowerShell when process creation reported a missing executable. That
made the UI unable to explain or control the selected shell, coupled working
directory changes to PowerShell syntax, and excluded Command Prompt and Git
Bash.

Shell discovery must not add registry and filesystem probes to the new-tab hot
path. Git Bash discovery must also avoid treating the Windows `bash.exe` WSL
compatibility entry point as Git Bash.

## Decision

- Persist one stable application-level preference: `automatic`,
  `powerShellCore`, `windowsPowerShell`, `commandPrompt`, or `gitBash`.
- Detect supported shells once at application startup and when the user asks to
  refresh the settings list.
- Resolve `automatic` as PowerShell 7, Windows PowerShell, then Command Prompt.
  Git Bash is opt-in and never takes over the automatic default.
- Locate Git Bash from Git for Windows installation metadata or the Git
  executable's installation root; never accept `System32\\bash.exe`.
- Pass an explicit executable path, argument list, working directory, display
  name, and integration capability to the local terminal backend.
- Pass the executable separately as `CreateProcessW.lpApplicationName`; the
  command line still contains a quoted argv[0].
- Reuse OSC 633 integration for PowerShell 7 and Windows PowerShell. Command
  Prompt and Git Bash initially run with basic terminal semantics.
- If a manually selected shell is no longer installed, preserve the saved
  preference but transparently resolve this launch through the automatic
  fallback chain.

Application settings schema 29 adds `localShell`; schemas 1 through 28 migrate
to `automatic`.

## Consequences

The default remains familiar on existing systems while local terminal choice
becomes explicit and inspectable. Shell discovery is Windows-specific but kept
behind a catalog boundary. Future platform catalogs can produce the same launch
description without changing QML or terminal rendering.
