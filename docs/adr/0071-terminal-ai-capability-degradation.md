# ADR 0071: Terminal AI capability degradation

Status: accepted

## Context

PowerShell, bash, zsh, fish, nested SSH, privilege prompts, tmux, and
alternate-screen applications do not provide equally trustworthy command
boundaries. A rendered frame can still be exact while command lifecycle claims
are approximate or unavailable. Treating these as one capability would let an
agent invent exit status or wait on boundaries that were never observed.

## Decision

- Adapt every live frame result into two independent dimensions: rendered-frame
  observation and semantic command quality.
- Map a nonce-verified rich lifecycle to `rich_verified`, with exact command
  boundaries and reliable exit status. Map ordinary OSC markers to
  `basic_unverified`, without exact-boundary or reliable-status claims. Map no
  integration to `unavailable`.
- Identify supported shell families (`powershell`, `bash`, `zsh`, and `fish`)
  only from observed shell metadata. Unknown values remain `unknown`.
- Mark alternate-screen observation explicitly and continue using bounded frame
  deltas even when command semantics are unavailable.
- Include a machine-readable degradation reason in tool results. Nested SSH,
  sudo, and tmux without verified marker passthrough therefore degrade rather
  than inheriting the outer shell's trust.
- Keep the adapter provider-independent and cover the supported shell and
  degraded interactive fixtures in domain tests.

## Consequences

- The agent can choose frame waits for interactive programs without confusing
  them with semantic command completion.
- Explain-failure and exit-code reasoning remain capability-gated.
- Remote shell installation or dotfile mutation is not required for safe basic
  operation; richer integration remains explicit and separately consented.
