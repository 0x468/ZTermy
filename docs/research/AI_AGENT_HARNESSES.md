# AI agent harness comparison

Status: implementation baseline, 2026-08-13

## Scope

This review compares the interaction contracts of current coding and terminal
agents rather than copying their source or labels. The goal is to give ztermy a
native terminal-agent experience: fluid input, legible tool progress, useful
automation, and approvals that scale from one action to a trusted session.

## Findings

### Codex

Codex separates the approval policy from the execution sandbox. Its current
configuration exposes `untrusted`, `on-request`, `never`, and granular approval
policies independently from `read-only`, `workspace-write`, and
`danger-full-access` sandboxes. Reusable command approval is based on a scoped
command prefix rather than a vague approval of all future shell activity.

Transferable lessons:

- mode and execution boundary are separate dimensions;
- a request can offer a narrow reusable rule without forcing the user to write
  policy syntax manually;
- fully automatic operation is a real mode, not an ask mode with extra labels;
- commands are split at shell control operators before a reusable rule is
  evaluated.

Source: [Codex configuration reference](https://developers.openai.com/codex/config-reference),
[Codex on-request approval contract](https://github.com/openai/codex/blob/main/codex-rs/prompts/templates/permissions/approval_policy/on_request.md).

### Claude Code

Claude Code combines a permission mode with ordered `deny`, `ask`, and `allow`
rules. Its modes include normal/default, edit acceptance, plan, automatic,
`dontAsk`, and bypass operation. Tool-specific rules can match Bash commands,
file paths, web domains, and tools. Interactive approval can create a persisted
rule, while some grants intentionally last only for the current session.

Transferable lessons:

- the approval surface should contain the proposed action and the available
  duration in one place;
- deny, ask, and allow are rule outcomes, not separate products;
- per-capability matching is more predictable than one global danger score;
- users need an inspectable permission editor after rules have accumulated.

Source: [Claude Code permissions](https://code.claude.com/docs/en/permissions),
[Claude Code permission modes](https://code.claude.com/docs/en/permission-modes).

### OpenCode

OpenCode exposes primary/subagent modes and assigns `allow`, `ask`, or `deny`
per tool. Rules may be objects whose patterns select a decision, including Bash
commands and MCP tools. This makes the mode a convenient default while exact
tool policy remains configurable.

Transferable lessons:

- model/tool availability and permission decisions belong to the agent
  configuration, not prompt prose;
- MCP tools must enter the same visible rule system as native tools;
- a compact mode selector can coexist with an advanced rule editor.

Source: [OpenCode agents](https://opencode.ai/docs/agents),
[OpenCode permissions](https://opencode.ai/docs/permissions).

### Pi

Pi deliberately keeps its coding-agent core small: read, write, edit, and Bash
tools are available, while permission gates are extensions rather than mandatory
core ceremony. It independently exposes model thinking levels and whether the
thinking block is shown.

Transferable lessons:

- tool execution should remain composable and provider-neutral;
- policy is a replaceable local layer around tools, not behavior embedded in
  every tool implementation;
- model reasoning support and action permission mode are independent controls.

Source: [Pi coding agent](https://github.com/badlogic/pi-mono/blob/main/packages/coding-agent/README.md),
[Pi settings](https://github.com/badlogic/pi-mono/blob/main/packages/coding-agent/docs/settings.md),
[Pi extension examples](https://github.com/badlogic/pi-mono/blob/main/packages/coding-agent/examples/extensions/README.md).

### Hermes Agent

Hermes is explicitly a single-tenant personal agent. It offers broad toolsets
and interchangeable terminal backends: local, Docker, SSH, Singularity, Modal,
and Daytona. Its security description correctly treats process-internal
allowlists and scanners as policy heuristics rather than containment; meaningful
isolation comes from the selected execution backend and operating system.

Transferable lessons:

- expert-first local execution is a legitimate default;
- isolation, when desired, should be a concrete backend choice rather than
  repetitive modal prompts;
- one tool protocol can target local, container, or SSH execution without
  changing the conversation model.

Source: [Hermes tools and terminal backends](https://github.com/NousResearch/hermes-agent/blob/main/website/docs/user-guide/features/tools.md),
[Hermes security model](https://github.com/NousResearch/hermes-agent/security).

### Warp and terminal-native agents

Warp's open client demonstrates why a terminal agent needs command blocks,
long-running command reads, PTY input, cancellation, and explicit lifecycle
states. The visible conversation is Markdown-oriented, but tool cards remain
structured native controls rather than model-authored Markdown.

Transferable lessons:

- terminal output must return through the tool result that owns it; asking the
  model to recover it from a viewport is lossy;
- user keystrokes and agent actions share one PTY and therefore need internal
  serialization, but this does not justify a daily “take control” workflow;
- cancellation, retry, and approval are lifecycle operations and must remain
  responsive even while provider or terminal work is pending.

Source: [Warp Full Terminal Use](https://docs.warp.dev/agent-platform/capabilities/full-terminal-use),
[Warp agent profiles and permissions](https://docs.warp.dev/agent-platform/capabilities/agent-profiles-permissions).

## ztermy synthesis

ztermy separates four concepts:

1. **Mode** — the default decision for a class of action.
2. **Rule** — a narrow exception matched by capability and action text/target.
3. **Execution lease** — an invisible internal serialization primitive that
   prevents user and agent bytes from interleaving in one PTY write.
4. **Backend boundary** — the actual local/SSH/SFTP/MCP target on which the
   action operates.

The product modes are:

| Mode | Reads | Terminal commands | File/SFTP edits | Prompts |
| --- | --- | --- | --- | --- |
| Read-only | automatic | denied | denied | none |
| Ask | automatic | ask unless a rule matches | ask unless a rule matches | per unmatched write |
| Edit | automatic | ask unless a rule matches | automatic in the selected scope | terminal writes only |
| Auto | automatic | automatic unless an ask/deny rule matches | automatic unless an ask/deny rule matches | exceptions only |
| YOLO | automatic | automatic | automatic | none |

Invalid schemas, closed/stale targets, unavailable capabilities, and exhausted
technical budgets are errors in every mode; they are not user approval prompts.
YOLO bypasses heuristic risk warnings. Direct user Run remains exact approval
and never receives a second confirmation.

Ask-mode approval offers:

- Run once;
- Allow a suggested exact/prefix rule for this session;
- Always allow the editable rule;
- Deny once;
- Always deny the editable rule.

Rules are typed by capability and use `exact`, `prefix`, `glob`, `regex`, or
`all` matching. Their duration is `once`, `session`, `profile`, or `global`.
Precedence is invariant failure, explicit deny, explicit ask, explicit allow,
then mode default. Permanent rules are visible, editable, exportable, and
revocable from Settings.

## Presentation contract

- Assistant text is rendered as Markdown with headings, lists, tables, links,
  quotes, inline code, and fenced code blocks.
- Copy always returns the untouched model text, not rendered plain text or HTML.
- Provider-exposed reasoning/summary is a separately collapsible stream. Hidden
  chain-of-thought is neither requested nor fabricated.
- Tool calls use native cards with queued/running/waiting/completed/failed/
  cancelled states and never rely on Markdown for buttons.
- Cancel is immediate in the UI, idempotent, and cannot be overwritten by a
  synchronous completion callback. Retry creates a new request from the last
  user turn without duplicating that user message.
- User typing is always accepted by the ordered PTY input queue and does not
  cancel an Agent turn merely to change an ownership flag. The internal lease
  serializes Agent conversations only; no persistent “take control” widget is
  shown in the normal flow.
