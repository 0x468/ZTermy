# V3 AI program

Status: `0.3.0`-`0.3.9` implementation complete; deeper NetCatty-class context
and Agent ecosystem parity is planned for `0.3.10`-`0.3.11`; release-candidate
human and environment-dependent acceptance pending

## Decision

V3 focuses on native AI assistance and terminal agency. Cross-platform work,
cloud collaboration, serial support, and a general remote IDE remain separate
future decisions. All `0.3.z` releases share one owner-selected codename. The
accepted `0.3.x` identity is:

- codename: **糸**;
- verse: **「剪不断，理还乱，是离愁」**.

This identity remains unchanged for every `0.3.z` patch release.

The central V3 product is not a chat box attached to a terminal. It is a
provider-independent, auditable AI runtime built on semantic terminal state:

```text
terminal bytes + shell lifecycle + user selection + session metadata
    -> bounded semantic context
    -> provider-independent conversation and tool protocol
    -> explicit native tools
    -> terminal / SSH / SFTP / telemetry actions
```

The supporting research is in `research/AI_TERMINAL_LANDSCAPE.md` and
`research/AI_AGENT_HARNESSES.md`; the target
design is in `AI_ARCHITECTURE.md`; security boundaries are in
`AI_SECURITY_AND_PRIVACY.md`; acceptance is in `testing/V3_AI_ACCEPTANCE.md`;
implementation-to-test traceability is in
`testing/V3_IMPLEMENTATION_TRACEABILITY.md`; the first formal review response is in
`reviews/V3_AI_ARCHITECTURE_REVIEW_2026-08-11.md`; the owner-facing final
candidate checklist is `testing/V3_OWNER_ACCEPTANCE_ZH_CN.md`. The current
end-to-end product and implementation audit is
`reviews/V3_AI_SYSTEM_REVIEW_2026-08-14.md`; it supersedes earlier mode and
approval descriptions where they conflict.

## Product outcomes

V3 succeeds when ztermy can:

- explain a selected terminal region or the last failed command with the exact
  command, output, exit status, working directory, host, and provenance;
- translate natural language into a reviewable command, then Insert or Run it
  without forcing a redundant warning after an explicit user Run action;
- run bounded multi-step terminal tasks, read their results, wait for long
  commands, interrupt them, and coexist with ordinary user input;
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
   types. OpenAI Responses, Anthropic Messages, OpenAI-compatible endpoints, and
   Ollama are adapters.
4. **Expert-first defaults.** ztermy is a personal tool for technical users.
   Follow mainstream direct workflows, keep setup compact, and keep necessary
   safety machinery transparent unless a concrete risk requires interaction.
5. **Professional autonomy.** Direct Run is available. Observer, confirmation,
   and automatic modes are configurable; approval friction is not hard-coded as
   the product's personality.
6. **Untrusted terminal data.** Remote output can contain prompt injection,
   escape sequences, or secrets. It is evidence, never authority.
7. **Explicit terminal context.** A normal question contains the accumulated AI
   conversation, not ambient terminal activity. The user explicitly attaches a
   selection or recent semantic command blocks. Attached evidence and Agent tool
   results remain bounded conversation evidence for coherent follow-up turns.
8. **No UI-thread I/O.** Model networking, token estimation beyond small local
   work, compaction, encryption, and tool execution never block Qt Quick.
9. **No inert breadth.** A visible tool ships with backend behavior, errors,
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
- OpenAI Responses, Anthropic Messages, generic OpenAI-compatible, and Ollama
  native streaming adapters; branded presets cover OpenAI, Anthropic, DeepSeek,
  Kimi, and Z.AI without leaking protocol selection into the UI;
- integrated provider/API-address/API-key/model settings, asynchronous model
  discovery, and editable model fallback using the existing installed/portable
  credential boundary transparently;
- bounded `CommandBlockStore` with capability quality (`none`, `basic`, `rich`),
  command text, output, exit status, timestamps, CWD, host, attribution, and
  explicit output coverage (`complete`, bounded/truncated, gapped, interleaved,
  or unknown);
- OSC 133 plus compatible OSC 633 lifecycle handling, including verified nonce
  support for ztermy-owned rich integration;
- AI terminal side panel, explicit selection/recent-command attachments,
  removable context chips, context preview,
  evidence-quality badges, streaming answer, cancel/retry, selection attachment,
  ordinary copy behavior backed by the existing clipboard implementation, and
  keyboard/focus behavior;
- Explain selection, Explain last failure, Generate command, Insert, and explicit
  Run; Explain last failure is capability-gated and never invents a failure when
  exit status is unavailable;
- read-only tools for active session metadata, terminal range/frame, and command
  blocks;
- local redaction, concrete bounded context defaults, usage/latency/token and
  estimated-cost reporting, bounded provider backoff, remembered last model,
  session-only conversation retention by default, and visible New conversation
  plus History actions.

The first release does not control arbitrary interactive applications, expose
SFTP writes, run unattended background agents, or depend on MCP.

### 0.3.1 — terminal action agent

- typed `run_command`, `read_command_output`, `wait_command`, `interrupt_command`,
  and `write_to_pty` tools;
- replay-safe, at-most-once native dispatch within an active conversation through
  a retained turn/tool-call deduplication record; side effects are never blindly
  retried;
- read-only, ask, edit, auto, and YOLO modes with typed deny/ask/allow rules,
  once/session/profile/global grants, visible target identity,
  per-turn action/time/token budgets, and loop detection;
- long-running command snapshots, alternate-screen metadata, and ordered
  user/Agent PTY input;
- one Agent write lease per terminal session, read-only observer fanout,
  queued/cancelled/completed tool cards, exact action results, and bounded retry;
- encrypted conversation history enabled by default when a persistent vault is
  available, with explicit disable, audit metadata, export, and deletion;
- a user-visible AI activity/audit view with redacted metadata and tool outcomes.

The activity/audit half is implemented as a bounded metadata-only trail in ADR
0060. ADR 0061 separately implements the encrypted store format, vault-only data
key, retention, tamper/key-loss handling, serialized application I/O, opt-in
settings, browsing/replay, export, and deletion. Audit therefore never silently
becomes conversation retention.

### 0.3.2 — SSH operations intelligence

- read-only SFTP navigation and file-reading tools with byte and path bounds;
- opt-in SFTP mutation tools using the existing transfer job graph;
- telemetry, forwarding status, scripts, notes, and shell-history tools;
- current-terminal operations implicitly bound to the owning AI sidebar, with
  host-side reconnect-generation validation and no model-visible session routing;
- reusable runbooks generated from successful conversations, stored as owned
  ztermy scripts rather than opaque model memory.

### 0.3.3 — robust interactive terminal use

- frame-delta observation for REPLs, database shells, debuggers, pagers, and
  full-screen applications;
- deterministic idle/change/wait conditions rather than sleep-only polling;
- explicit Agent pause/resume and interactive-wait states without blocking
  ordinary user terminal input;
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

### 0.3.5 — agent conversation repair

- return the finished command's bounded normalized output directly from
  `wait_command`, so the model does not reconstruct results from a viewport;
- make cancel idempotent and immediately responsive, represent cancellation as
  a neutral retryable lifecycle state, and prevent synchronous cancellation
  callbacks from restoring a stale `cancelling` state;
- render assistant text and provider-exposed reasoning as Markdown while Copy
  preserves the untouched provider source;
- add an explicit local JSONL provider trace for opt-in debugging of the exact
  bounded request, terminal context, tool continuation, and raw response;
- remove visible terminal-control ceremony from ordinary conversation while
  retaining deterministic PTY write serialization internally.

### 0.3.6 — mainstream modes and reusable rules

- replace legacy observer/first-write/saved-host modes with Read-only, Ask,
  Edit, Auto, and YOLO;
- add capability-specific exact/prefix/glob/regex/all rules with allow/ask/deny
  decisions and once/session/profile/global duration;
- make approval cards offer Run once, session rule, permanent rule, deny once,
  and permanent deny with an editable suggested matcher;
- add a compact rule manager with search, enable/disable, edit, delete, import,
  and export.

### 0.3.7 — reasoning, tool cards, and rich Markdown

- expose provider/model reasoning capability and off/auto/effort controls only
  where the adapter can express them honestly;
- retain provider-exposed reasoning summaries across tool continuations without
  claiming access to hidden chain-of-thought;
- complete responsive native tool cards, Markdown tables/lists/code/links,
  raw-copy behavior, and narrow-panel/high-DPI visual acceptance;
- distinguish provider retry, user retry, tool wait, cancellation, and offline
  recovery with responsive non-blocking controls.

### 0.3.8 — autonomous terminal-agent closure

- exercise local and SSH agents against deterministic multi-step tasks in every
  mode, including user typing during a run and long/interactive commands;
- finalize edit/auto/YOLO behavior across terminal, SFTP, runbooks, and MCP with
  no accidental confirmation prompts;
- complete rule-precedence, replay, loop-budget, concurrency, latency, and
  provider compatibility gates;
- produce the owner-facing V3 acceptance matrix and release candidate artifacts.

### 0.3.9 — NetCatty-class conversation surface

- make the conversation the primary object with compact New, History, Export,
  and overflow actions;
- align the composer hierarchy around message input, explicit context, model,
  execution mode, and send/cancel without exposing implementation diagnostics
  as primary navigation;
- provide sticky streaming, Return to latest, auto-managed reasoning, per-code
  copy, local table/code overflow, and narrow-panel responsive layout;
- provide a keyboard-first built-in slash-command picker for conversation,
  history, context attachment, explanation, and command-generation actions;
- preserve explicit selection/recent-command evidence across follow-up turns,
  with a clearly labelled approximate fallback for shells without semantic
  command marks.

### 0.3.10 — explicit context and reusable skills

- add explicit local text/file and provider-capable image attachments with
  previews, removal, bounded ingestion, and no ambient filesystem scraping;
  the local UTF-8 text-file slice is implemented asynchronously with four-file
  and 256 KiB-per-source limits (ADR 0087), while image input remains pending;
- add reusable user-authored skills, surfaced through the same slash-command
  picker without coupling them to one provider;
- add optional web search as a typed Agent tool with visible provenance and
  citations;
- cover attachment, skill, and search persistence/compaction boundaries in the
  deterministic evaluation corpus.

### 0.3.11 — external Agent ecosystem

- expose an Agent selector that keeps the built-in ztermy Agent as the default;
- define native adapters for selected external Agent/SDK protocols without
  embedding a Web or Node runtime in the application;
- keep terminal target ownership, permission modes, audit, cancellation, and
  tool-result contracts identical across built-in and external Agents;
- close provider/Agent switching, resume, failure-recovery, and packaging
  acceptance before declaring the expanded V3 program complete.

Current closure work:

- a deterministic provider-independent Agent scenario now crosses mode
  evaluation, approval/automatic execution, real ConPTY/PowerShell, semantic
  command blocks, retained `wait_command` output, at-most-once replay, user
  input during a long command, and interactive PTY input (`ADR 0083`);
- the saved-SSH-Profile mutation matrix is covered without pretending that it
  replaces the separate real-host transport and shell-capability acceptance;
- an environment-gated real-host scenario drives Agent commands, concurrent
  user input, and interactive PTY input through the production SSH session;
- reviewed MCP tools now obey Read-only/Ask/Edit/Auto/YOLO and reusable
  `mcp-tool` rules instead of forcing a confirmation in every mode; executable
  trust and exact-schema review remain prerequisites (`ADR 0082`);
- cached MCP replay is resolved before consuming another turn-budget action,
  while automatic calls retain the same cancellation, audit, schema-identity,
  and untrusted-result contracts as approved calls.

Delivery status:

- the planned `0.3.0` through `0.3.4` implementation slices are present in the
  `0.3.0` candidate. Milestone numbers describe delivery slices inside V3, not
  separately shipped patch releases;
- the provider-independent evaluation corpus and deterministic replay harness
  are implemented (`ADR 0072`);
- local MCP stdio transport, isolated namespaces, schema-drift revocation, and
  bounded untrusted results are implemented (`ADR 0073`);
- persistent server management and the executable-trust -> exact-schema-review
  chain are integrated with cancellation, replay protection, audit, Settings
  UI, backup recovery, and the shared Agent mode/rule decision path (`ADR 0074`,
  `ADR 0082`);
- automatic task/model routing remains intentionally absent: the current replay
  evidence does not establish a provider-independent quality, latency, or cost
  improvement. The remembered explicit model remains the predictable default;
- privacy diagnostics now expose the live local request boundary and add only
  content-free AI counts/policy state to diagnostic exports (`ADR 0075`);
- provider-independent concurrency/lifecycle soak infrastructure and JSON
  evidence are implemented (`ADR 0076`); Debug and static Release developer
  runs are green, the final schema-2 static Release two-hour Agent/MCP duration
  gate completed 182 iterations in 7232.8 seconds with zero failures, and the
  AI-idle terminal gate completed 28800.1 seconds with zero failures;
- deterministic provider wire contracts and real-window responsive and
  accessibility contracts are implemented (`ADR 0077`, `ADR 0078`);
- the static `0.3.0` MSI and portable ZIP pass the inherited installer,
  manifest, checksum, portable-mode, and lifecycle contracts. That formal RC
  passed the then-current 1577/1577 translation gate, Debug and static Release
  suites at 105/105, and all 224 static clang-tidy translation units. The
  subsequent `0.3.9` developer slice passes the expanded 1619/1619 translation
  gate and Debug 105/105 suite; its final static package/tidy rerun remains a
  release-candidate gate rather than a developer-iteration claim;
- the exact automated evidence and remaining owner/environment checks are
  recorded in `testing/V3_0_3_0_RC_ACCEPTANCE.md`. Live-provider evaluation,
  real-host shell coverage, MSI interaction, and previous-Windows-build coverage
  remain owner/environment acceptance work rather than hidden implementation
  gaps.

## Explicit exclusions

- copying Warp, NetCatty, VS Code, Wave, or Tabby implementation or assets;
- making Warp's server-side AI backend a dependency;
- training or fine-tuning a model inside ztermy;
- silently sending the full scrollback, private keys, credentials, raw terminal
  keystrokes, or arbitrary local files to a provider;
- treating a visual terminal snapshot as proof of command success;
- a full code editor, repository indexing, cloud agents, collaboration, or sync;
- cross-terminal or multi-host orchestration from a terminal AI sidebar;
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
- Auto and YOLO are explicit expert modes; YOLO does not emit heuristic risk
  prompts, while profile-scoped exceptions are represented as rules;
- ephemeral shell integration is the default and an explicit, previewed,
  reversible persistent installer is allowed;
- command-risk classification is informative in Ask/Edit and audit views; it
  does not add surprise prompts to Auto, YOLO, or direct visible Run;
- direct visible Run remains exact authorization and receives no duplicate
  warning.

The release identity card uses the accepted `糸` codename and verse and remains
part of the real-window layout smoke for every `0.3.z` release candidate.
