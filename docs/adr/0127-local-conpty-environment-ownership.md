# ADR 0127: Own the local ConPTY child's terminal environment

- Status: Accepted
- Date: 2026-09-25

## Context

New local shells need the latest user and machine `PATH`, even if ztermy was
started before a program was installed. They also need transient variables from
ztermy's process. Merging a fresh registry-backed environment with the process
environment satisfies both needs, but can carry terminal identity from the
program that launched ztermy. In particular, `TERM=dumb` disables PowerShell's
VT support, and an inherited `WT_SESSION` incorrectly identifies a ztermy
child as a Windows Terminal session.

## Decision

- Build a fresh registry-backed environment for every local ConPTY child, then
  preserve transient process variables except where they describe the parent
  terminal.
- Set `TERM=xterm-256color` and `COLORTERM=truecolor` for the new child. Keep
  user opt-outs such as `NO_COLOR` unchanged.
- Remove inherited `WT_SESSION`, `WT_PROFILE_ID`, `WT_WINDOWID`, `TERM_PROGRAM`,
  and `TERM_PROGRAM_VERSION`. Do not claim a Windows Terminal session or invent
  a `TERM_PROGRAM` identity for ztermy.
- Apply this policy only to local ConPTY children. SSH terminal negotiation and
  remote environment policy remain separate.

## Consequences

Launching ztermy from a limited or different terminal no longer downgrades new
local shells or leaks the parent's terminal identity. The policy does not
refresh an already running shell's environment. As ztermy's VT compatibility
evolves, its advertised `TERM` must continue to match the behavior it actually
supports.

## References

- [PowerShell terminal environment variables](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_environment_variables#terminal-features)
- [Windows Terminal's `WT_SESSION` identity](https://github.com/microsoft/terminal/issues/2256)
