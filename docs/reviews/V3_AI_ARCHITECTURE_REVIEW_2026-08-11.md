# V3 AI architecture review response

Status: incorporated into the accepted V3 baseline, 2026-08-11

## Summary

The review found real structural gaps. Items 1, 2, 5, 6, 8, 9, 10, 11, 12,
13, and 14–19 are accepted. Items 3, 4, and 7 are accepted with a narrower or
more technically accurate resolution. No item is dismissed solely because
ztermy targets professional users.

The highest-priority corrections are now architecture, not backlog notes:

- semantic capture cannot silently lose output and still call it complete;
- side-effecting tool calls require replay-safe dispatch;
- shell integration has explicit ephemeral/persistent/disabled activation;
- one terminal session has one AI write owner;
- output reads, waits, interrupts, risk, retries, and turn budgets have native
  contracts rather than prompt conventions.

ADR 0055 and ADR 0056 own these foundations.

## Finding-by-finding disposition

| # | Disposition | Resolution |
| --- | --- | --- |
| 1 | Accept — critical | Split lossless-or-explicitly-gapped semantic journal capture from coalescible derived UI/provider observation. `finished` no longer implies output completeness. Blocks carry coverage, retained cursor range, and lost-byte/interleave metadata. |
| 2 | Accept — critical | Add a dispatch record keyed by conversation/turn/tool-call ID plus tool and canonical argument hash. Identical replay joins/caches; conflicting replay fails; ambiguous writes never auto-retry. See ADR 0056. |
| 3 | Partly accept — critical | Rich integration does require shell cooperation, but persistent dotfile mutation is not the only path. Default to ephemeral wrappers; offer a separately consented, previewed, backed-up, atomic, reversible installer. Never alter execution policy or tmux config automatically. See ADR 0055. |
| 4 | Accept with product distinction — high | Direct visible Run remains exact authorization without another warning. Model-initiated writes receive a deterministic high-risk overlay. Proposed automatic-mode default is Ask for high-risk, with only an explicit session/target grant able to relax it. |
| 5 | Accept — high | Explain last failure is Verified only with rich evidence, Approximate only when a basic integration actually observed a non-zero exit status, and disabled when status is unknown. |
| 6 | Accept — high | Replayed user/model messages and historical tool results are untrusted evidence. Old text never carries forward approval; trusted policy is reconstructed outside transcripts. |
| 7 | Accept with limits — high | Windows protected clipboard formats make history/cloud exclusion feasible and become the AI default. Minidump filtering is defense in depth, not a guarantee: dumps are Secret, never auto-uploaded/exported, use non-full-memory/filtering/short-lived scrubbed secret buffers, and are scanned with synthetic markers. |
| 8 | Accept — high for 0.3.4 | MCP server identity, transport, credentials, trust tier, schema/description changes, elicitation, and returned content become independent trust boundaries. MCP tools still pass native target/risk/dedup/watchdog policy. |
| 9 | Accept — high | Provider errors are classified. Auth/config errors do not retry. Transient 408/429/selected 5xx/reset failures get at most two jittered exponential retries and honor `Retry-After`; no retry may replay a side effect. |
| 10 | Accept — research gap | Add Codex and Claude Code as agent-harness comparisons: sandbox vs approval, grant lifetime, pre-tool hooks, automated review, loop/retry limits, and permission fatigue. |
| 11 | Accept with protocol correction — research gap | Add Windows Terminal OSC 133 and Microsoft Intelligent Terminal. OSC 633 is the VS Code extension family, not the Windows Terminal protocol documented by Microsoft. Intelligent Terminal is especially relevant: native ConPTY, ACP agent pane, strict target selection, and real profile/`.bashrc` installation behavior. |
| 12 | Accept with legal/engineering nuance — source boundary | The local NetCatty tree is GPL-3.0. Reading GPL source does not by itself mechanically “infect” independent code, but copying protected expression or translating its implementation is outside ztermy's boundary. Research paths remain provenance only; ztermy must not port source, identifiers, algorithms, or file structure. Future design uses black-box product behavior, public protocols, and independently written contracts. |
| 13 | Accept — evaluation gap | Define deterministic 100% protocol/safety gates, five runs/task, >=4 successes/task, >=90% aggregate, zero target/approval/duplicate/policy violations, >=95% evidence completeness, and human factual rubrics. Track a pinned Terminal-Bench subset as non-exclusive external evidence. |
| 14 | Accept — product trust | Context items can be removed or pinned. Pinning changes priority only and never bypasses redaction or hard budgets. |
| 15 | Accept — cost/control | Show exact provider usage when available and dated estimated cost when pricing is known. Add enforced per-turn call/write/time/retry/repeated-read/token-cost budgets and visible stop reasons. |
| 16 | Accept — concurrency | One terminal has one AI write/control owner and multiple read-only observers. Wait cancellation is subscriber-local; ownership transfer is explicit. |
| 17 | Accept — tool contract | Reads are cursor-based; waits are subscriptions; soft interrupt sends the PTY-equivalent Ctrl+C and is not a kill guarantee; session close and future PID/signal operations are separate capabilities. |
| 18 | Accept — data retention | Distinguish retained-more from evicted/gapped output. Return `cursor_expired` and coverage; never silently rerun a command. Optional user-enabled logging is separate evidence, not assumed recovery. |
| 19 | Accept — UX trust | Context chips and explanations display `Verified`, `Approximate`, `Raw`, or `Partial`; these labels describe evidence quality, not whether output content is truthful. |

## Smaller findings

- `0.3.0` automatic context now defaults to the requested/failed block, five
  preceding completed blocks, and one relevant current frame; aggregate 64 KiB,
  1,000 normalized lines, and estimated 16k input tokens, with 16 KiB/300 lines
  per item.
- Provider/model selection remembers the last successful choice in `0.3.0`;
  reusable routing profiles remain later work.
- A user-visible AI activity/audit surface ships with action tooling rather than
  leaving `AiAuditStore` as an invisible implementation detail.
- Acceptance now fixes soak durations and records the exact Windows/Qt/shell/
  SSH/provider/model/build matrix.

## Evidence added after review

- [Windows Terminal shell integration](https://learn.microsoft.com/en-us/windows/terminal/tutorials/shell-integration)
- [Microsoft Intelligent Terminal](https://github.com/microsoft/intelligent-terminal)
- [Intelligent Terminal integration installer](https://github.com/microsoft/intelligent-terminal/blob/main/doc/installing-dependencies.md)
- [OpenAI Codex sandboxing and approvals](https://learn.chatgpt.com/docs/sandboxing)
- [Claude Code hooks](https://code.claude.com/docs/en/hooks)
- [Terminal-Bench/Harbor](https://github.com/harbor-framework/terminal-bench)
- [Windows clipboard history/cloud formats](https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats)
- [Windows minidump filtering flags](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/ne-minidumpapiset-minidump_type)

## Owner decisions resolved

The owner accepted the recommended defaults on 2026-08-11:

1. allow the explicit persistent shell-integration installer in addition to the
   default ephemeral mode;
2. keep high-risk model actions at Ask by default even in automatic mode, while
   permitting a session-scoped advanced override;
3. keep encrypted AI history opt-in and session-only by default;
4. show OpenAI Responses first, then Ollama and generic compatible endpoints;
5. keep saved-host automatic execution behind an advanced setting.

The shell-startup installer rule is intentionally narrow. It does not ban
ordinary dotfiles: `.env` and similar application files may be read or edited by
an explicitly scoped tool according to content sensitivity and action policy,
but secret-bearing content is not automatically sent wholesale to a provider.
