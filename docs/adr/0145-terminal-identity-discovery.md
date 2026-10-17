# Truthful terminal identity and best-effort SSH hints

Status: Accepted

Ztermy identifies itself, not its rendering dependency or a parent terminal.
`TERM_PROGRAM=ztermy`, `TERM_PROGRAM_VERSION` from the generated application
version and `COLORTERM=truecolor` describe local children. Foreign parent
terminal markers are removed. `TERM=xterm-256color` stays the compatible local
default; a private terminfo database is not installed on remote hosts.

The standard XTVERSION query (`CSI > q`, including explicit parameter zero)
returns `DCS >| ztermy(version) ST`. This callback remains installed after a
terminal reset. Graphics and size queries continue to report actual engine
capabilities, independently of terminal identity. There are no external-Agent
runtime integrations, application-specific allowlists or branding overrides.

SSH requests these three identity/color variables before starting the shell.
They are best-effort hints: an explicit channel environment-request rejection
does not abort the session. I/O, timeout, cancellation and protocol errors are
not suppressed. Explicit profile variables take precedence and preserve their
existing required-request behavior. No shell commands are injected to bypass
server environment policy, and no remote files or sshd settings are changed.

This change does not add persisted fields or change either application or data
schema versions. Applications that only recognize known terminal names may
still need their own compatibility update; truthful identity is not a promise
that every client enables every supported graphics protocol automatically.
