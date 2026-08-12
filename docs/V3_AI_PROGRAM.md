# V3 AI program

Status: approved product program for `0.3.x`; implementation not started

## Decision

V3 focuses on native AI assistance and terminal agency. Cross-platform work,
cloud collaboration, serial support, and a general remote IDE remain separate
future decisions. All `0.3.z` releases share one owner-selected codename. The
provisional `0.3.x` identity is:

- codename: **糸**;
- verse: **「剪不断，理还乱，是离愁」**.

The owner confirms the final typography and presentation before the first
`0.3.0` release candidate.

The central V3 product is not a chat box attached to a terminal. It is a
provider-independent, auditable AI runtime built on semantic terminal state:

```text
terminal bytes + shell lifecycle + user selection + session metadata
    -> bounded semantic context
    -> provider-independent conversation and tool protocol
    -> explicit native tools
    -> terminal / SSH / SFTP / telemetry actions
```

The supporting research is in `research/AI_TERMINAL_LANDSCAPE.md`; the target
design is in `AI_ARCHITECTURE.md`; security boundaries are in
`AI_SECURITY_AND_PRIVACY.md`; acceptance is in `testing/V3_AI_ACCEPTANCE.md`;
the first formal review response is in
`reviews/V3_AI_ARCHITECTURE_REVIEW_2026-08-11.md`.

## Product outcomes

V3 succeeds when ztermy can:

- explain a selected terminal region or the last failed command with the exact
  command, output, exit status, working directory, host, and provenance;
- translate natural language into a reviewable command, then Insert or Run it
  without forcing a redundant warning after an explicit user Run action;
- run bounded multi-step terminal tasks, read their results, wait for long
  commands, interrupt them, and hand control between user and agent;
- understand an interactive terminal frame without pretending that a full-screen
  application is ordinary line-oriented scrollback;
- use relevant SSH, SFTP, telemetry, history, scripts, and notes through typed
  tools instead of prompt-only conventions;
- support cloud and local providers without leaking provider JSON into domain or
  QML code;
- show exactly what context and target session an action uses, redact likely
  secrets before provider transmission, and remain useful in a read-only mode;
- preserve the V2 terminal latency, cancellation, shutdown, credential, and
  diagnostic guarantees when AI is disabled, idle, streaming, or cancelled.

## Product principles

1. **Terminal semantics before chat polish.** Command boundaries and terminal
   frames are the durable foundation; visual chat work cannot substitute for
   them.
2. **Local-first orchestration.** Provider calls may be remote, but context
   selection, redaction, permissions, tools, audit metadata, and session control
   remain in ztermy.
3. **Provider independence.** A conversation and tool call have ztermy-owned
   types. OpenAI Responses, OpenAI-compatible endpoints, and Ollama are adapters.
4. **Professional autonomy.** Direct Run is available. Observer, confirmation,
   and automatic modes are configurable; approval friction is not hard-coded as
   the product's personality.
5. **Untrusted terminal data.** Remote output can contain prompt injection,
   escape sequences, or secrets. It is evidence, never authority.
6. **No hidden global scrape.** The user can inspect, remove, or pin individual
   context items in a bounded preview. Pinning never bypasses redaction or hard
   limits. Automatic context has declared provenance, evidence quality, and size.
7. **No UI-thread I/O.** Model networking, token estimation beyond small local
   work, compaction, encryption, and tool execution never block Qt Quick.
8. **No inert breadth.** A visible tool ships with backend behavior, errors,
   cancellation, tests, and a permission contract in the same milestone.

## Scope model

AI conversations have one explicit scope:

| Scope | Meaning | Initial availability |
| --- | --- | --- |
| Terminal | One local or SSH session and its command blocks | `0.3.0` |
| Workspace | The currently restored split-pane workspace and selected sessions | `0.3.2` |
| Host | Saved profile metadata plus an attached session | `0.3.2` |
| Global | Cross-host memories or unattended orchestration | Not in initial V3 |

A conversation always displays its scope and target identity. A tool cannot act
on another session merely because the model supplied a different identifier.

## Milestones

### 0.3.0 — semantic assistant foundation

Deliver the smallest complete vertical slice:

- `AiProvider` and typed streaming events;
- OpenAI Responses provider, generic OpenAI-compatible provider, and Ollama
  native streaming provider;
- provider/API-key settings using the existing installed/portable credential
  boundary;
- bounded `CommandBlockStore` with capability quality (`none`, `basic`, `rich`),
  command text, output, exit status, timestamps, CWD, host, attribution, and
  explicit output coverage (`complete`, bounded/truncated, gapped, interleaved,
  or unknown);
- OSC 133 plus compatible OSC 633 lifecycle handling, including verified nonce
  support for ztermy-owned rich integration;
- AI terminal side panel, removable/pinnable context chips, context preview,
  evidence-quality badges, streaming answer, cancel/retry, selection attachment,
  secure-copy behavior, and keyboard/focus behavior;
- Explain selection, Explain last failure, Generate command, Insert, and explicit
  Run; Explain last failure is capability-gated and never invents a failure when
  exit status is unavailable;
- read-only tools for active session metadata, terminal range/frame, and command
  blocks;
- local redaction, concrete bounded context defaults, usage/latency/token and
  estimated-cost reporting, bounded provider backoff, remembered last model,
  and session-only conversation retention by default.

The first release does not control arbitrary interactive applications, expose
SFTP writes, run unattended background agents, or depend on MCP.

### 0.3.1 — terminal action agent

- typed `run_command`, `read_command_output`, `wait_command`, `interrupt_command`,
  and `write_to_pty` tools;
- replay-safe, at-most-once native dispatch within an active conversation through
  a retained turn/tool-call deduplication record; side effects are never blindly
  retried;
- observer, ask-each-write, ask-first-write, session-auto, and saved-host-auto
  modes with deny/allow rules, a high-risk overlay, visible target identity,
  per-turn action/time/token budgets, and loop detection;
- long-running command snapshots, alternate-screen metadata, and user/agent
  control handoff;
- one write-owning conversation per terminal session, read-only observer fanout,
  queued/cancelled/completed tool cards, exact action results, and bounded retry;
- encrypted optional conversation history, audit metadata, export, and deletion;
- a user-visible AI activity/audit view with redacted metadata and tool outcomes.

The activity/audit half is implemented as a bounded metadata-only trail in ADR
0060. Encrypted transcript persistence remains a separate opt-in deliverable so
audit never silently becomes conversation retention.

ADR 0061 now implements and tests the encrypted store format, vault-only data
key, retention, tamper/key-loss handling, export, and deletion. Opt-in settings,
serialized application I/O, and history browsing/replay remain the integration
slice before `0.3.1` closes.

### 0.3.2 — SSH operations intelligence

- read-only SFTP navigation and file-reading tools with byte and path bounds;
- opt-in SFTP mutation tools using the existing transfer job graph;
- telemetry, forwarding status, scripts, notes, and shell-history tools;
- workspace-scoped tasks with an explicit target set and per-target results;
- reusable runbooks generated from successful conversations, stored as owned
  ztermy scripts rather than opaque model memory.

### 0.3.3 — robust interactive terminal use

- frame-delta observation for REPLs, database shells, debuggers, pagers, and
  full-screen applications;
- deterministic idle/change/wait conditions rather than sleep-only polling;
- explicit takeover, pause, resume, and transfer-control states;
- capability adapters and acceptance fixtures for PowerShell, bash, zsh, fish,
  nested SSH, `sudo`, `tmux`, and alternate-screen programs;
- degraded behavior when rich shell integration is absent or spoofing checks
  fail.

### 0.3.4 — extensibility and quality closure

- MCP client support behind the same tool permission and provenance model, with
  explicit server trust, credential, transport, tool-description, schema-change,
  and namespace boundaries;
- reusable provider/model profiles and task-specific model routing only after
  evaluation proves a quality, latency, or cost benefit;
- a versioned AI evaluation corpus, replay harness, provider contract tests,
  privacy diagnostics, and long-duration concurrency gates;
- complete English/Chinese, accessibility, package, upgrade, recovery, and
  manual real-host acceptance for the V3 surface.

## Explicit exclusions

- copying Warp, NetCatty, VS Code, Wave, or Tabby implementation or assets;
- making Warp's server-side AI backend a dependency;
- training or fine-tuning a model inside ztermy;
- silently sending the full scrollback, private keys, credentials, raw terminal
  keystrokes, or arbitrary local files to a provider;
- treating a visual terminal snapshot as proof of command success;
- a full code editor, repository indexing, cloud agents, collaboration, or sync;
- autonomous multi-host changes without an explicit target set and permission
  policy;
- storing large transcripts in Windows Credential Manager.

## Release gates

Every `0.3.z` release must satisfy all inherited V2 gates plus the AI matrix:

- deterministic provider-stream parsing, cancellation, reconnect, timeout, and
  malformed-event tests;
- schema-validated tool calls and results with target, generation, size, and
  state bounds, replay/deduplication checks, one-writer ownership, and explicit
  output-coverage metadata;
- adversarial terminal-output fixtures, secret-redaction fixtures, and prompt
  injection tests;
- representative task evaluations with correctness, unsafe-action, unnecessary
  confirmation, duplicate-side-effect, loop, latency, token, and cost
  measurements, using deterministic graders where possible and a documented
  human rubric otherwise;
- a real-window dark/light, keyboard, narrow panel, high-DPI, streaming,
  approval, cancellation, error, offline, and provider-auth review;
- real PowerShell and SSH command-block tests, including failure, long output,
  background output, alternate screen, disconnect, and shutdown;
- AI-disabled and AI-idle terminal soak evidence proving no regression from the
  existing 16 ms input-queue P95 budget.

## Approved product defaults

The owner approved these defaults on 2026-08-11:

- encrypted AI history is opt-in; conversations remain session-only by default;
- provider order is OpenAI Responses, Ollama, then generic OpenAI-compatible,
  while the architecture remains provider-independent;
- saved-host automatic execution is available only through advanced settings;
- ephemeral shell integration is the default and an explicit, previewed,
  reversible persistent installer is allowed;
- model-initiated high-risk actions ask by default even in automatic mode; an
  advanced grant may relax this only for the current session and exact target;
- direct visible Run remains exact authorization and receives no duplicate
  warning.

The remaining presentation decision is final typography/layout of the
provisional `糸` codename and verse before the first `0.3.0` release candidate.
