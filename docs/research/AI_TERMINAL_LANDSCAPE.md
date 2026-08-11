# AI terminal landscape and Warp open-source review

Status: research baseline, 2026-08-11

## Evidence rules

This review separates confirmed implementation evidence from product inference.
The Warp source baseline is commit
`cd49bd7fe672863e83bbe1ef45491fc30d29e205`. NetCatty evidence comes from the
local reference tree at `D:/tmp/Netcatty`, whose root `LICENSE` declares GPL-3.0;
it is a study source only. No source, assets, strings, implementation structure,
or branding from a reference product may enter ztermy. NetCatty source paths in
this document establish research provenance, not an implementation blueprint;
new ztermy contracts are derived independently from product requirements and
interoperable protocols.

Warp's repository licenses `warpui_core` and `warpui` under MIT and the rest of
the client under AGPL-3.0. ztermy therefore uses the repository to understand
product boundaries and independently designs its C++/QML implementation.

## Corrected Warp finding

Warp's client is now open source at
[`warpdotdev/warp`](https://github.com/warpdotdev/warp). This materially improves
what can be verified about terminal blocks, permissions, client-side action
execution, Full Terminal Use, conversation presentation, and cancellation.

It does **not** expose the whole hosted AI service. Warp's open-source
announcement states that model routing, model selection, billing, streaming
orchestration, and conversation state remain on Warp servers. The correct lesson
for ztermy is therefore split:

- study Warp's native/client semantic terminal and tool boundary;
- do not assume Warp provides a reusable local model-orchestration backend;
- keep ztermy provider calls and conversation orchestration local and
  provider-independent.

Sources: [Warp repository](https://github.com/warpdotdev/warp),
[open-source announcement](https://github.com/warpdotdev/warp/discussions/9240).

## Warp: code-level findings

### Blocks are the semantic substrate

Warp's `Block` owns command/prompt and output grids. Its client can produce a
separate command and output pair, enforce a serialized-output line cap, and run
secret redaction before the data leaves the block boundary. This is more than a
visual grouping.

Evidence:

- [`Block::command_and_output_with_secret_obfuscated`](https://github.com/warpdotdev/warp/blob/cd49bd7fe672863e83bbe1ef45491fc30d29e205/app/src/terminal/model/block.rs#L1786-L1813)
- [Blocks product contract](https://docs.warp.dev/terminal/blocks)
- [Blocks as explicit/automatic Agent context](https://docs.warp.dev/agent-platform/local-agents/agent-context/blocks-as-context)

Implication: ztermy needs a bounded command-block domain model. Scraping the last
N rendered lines remains a degraded fallback, not the primary AI context path.

### Shell tools distinguish lifecycle states

Warp's client distinguishes:

- request a command and collect its result;
- read an already running command;
- return a finished result with exit code and timestamps;
- return a live grid snapshot with cursor and alternate-screen state;
- write to the PTY of a long-running command;
- transfer terminal control back to the user;
- report cancellation or a missing block.

Evidence:

- [`ShellCommandExecutor`](https://github.com/warpdotdev/warp/blob/cd49bd7fe672863e83bbe1ef45491fc30d29e205/app/src/ai/blocklist/action_model/execute/shell_command.rs#L59-L927)
- [`action_result_for_read_shell_command_output`](https://github.com/warpdotdev/warp/blob/cd49bd7fe672863e83bbe1ef45491fc30d29e205/app/src/ai/blocklist/action_model/execute/shell_command.rs#L839-L883)
- [Full Terminal Use](https://docs.warp.dev/agent-platform/capabilities/full-terminal-use)

Implication: a single `execute(command) -> text` interface is insufficient for
REPLs, prompts, pagers, `top`, editors, debuggers, or servers.

### Permissions are capability-specific

Warp separates command execution from PTY writing and other capabilities. It
supports profiles, command allow/deny regular expressions, directory rules,
first-write behavior, and run-until-completion. Its client checks finished block
state and permission state before automatic actions.

Evidence:

- [`ShellCommandExecutor::should_autoexecute`](https://github.com/warpdotdev/warp/blob/cd49bd7fe672863e83bbe1ef45491fc30d29e205/app/src/ai/blocklist/action_model/execute/shell_command.rs#L107-L189)
- [`BlocklistAIPermissions`](https://github.com/warpdotdev/warp/blob/cd49bd7fe672863e83bbe1ef45491fc30d29e205/app/src/ai/blocklist/permissions.rs)
- [Profiles and permissions](https://docs.warp.dev/agent-platform/capabilities/agent-profiles-permissions)

Implication: ztermy should expose professional autonomy while retaining a typed
policy layer. Direct user Run is explicit authorization; background or model-
initiated actions follow the selected policy.

### Warp's limitations are also instructive

Warp documents that background-process output attribution can be ambiguous and
interleaved. Its open-source roadmap discussion also shows that secret redaction
of arbitrary terminal output remains a hard problem. These are not problems a
larger prompt can solve.

Sources: [background blocks](https://docs.warp.dev/terminal/blocks/background-blocks),
[Warp roadmap discussion](https://github.com/warpdotdev/warp/issues/9233).

## VS Code: shell integration before AI

VS Code's strongest transferable idea is not its editor agent. It is the shell
integration protocol that turns otherwise opaque PTY output into command
semantics.

Confirmed behavior:

- OSC 633 marks prompt start/end, pre-execution, execution completion, command
  line, working directory, and rich-detection quality;
- an optional nonce authenticates the exact-command sequence and mitigates
  command spoofing;
- integrations have `none`, `basic`, and `rich` quality;
- command execution APIs can stream output when integration is present; without
  it, completion detection necessarily falls back to weaker heuristics;
- remote/nested shells and unusual startup configurations can prevent automatic
  injection.

Sources: [VS Code terminal shell integration](https://code.visualstudio.com/docs/terminal/shell-integration),
[VS Code API reference](https://code.visualstudio.com/api/references/vscode-api),
[VS Code agent tools](https://code.visualstudio.com/docs/agents/concepts/tools).

Implication: ztermy should support OSC 133 interoperability and a richer
ztermy-owned/OSC-633-compatible path with a nonce. Capability quality must be
visible to the context broker; it cannot silently claim an exact command or exit
status in basic mode.

## Windows Terminal and Microsoft Intelligent Terminal

Windows Terminal is the most relevant native/ConPTY baseline. Microsoft documents
OSC 133 prompt, command-start, command-executed, and command-finished marks,
including the optional exit code. It explicitly states that the shell must
cooperate and that enabling the feature modifies the shell prompt.

Microsoft's newer open-source Intelligent Terminal fork adds a native agent pane
over Windows Terminal and makes the installation trade-off concrete:

- shell integration supplies OSC 133 boundaries and exit codes used by error
  detection and agent context;
- its first-run experience appends guarded integration to PowerShell profiles
  and bash `.bashrc`; cmd, zsh, fish, and Nushell remain unsupported in the
  documented baseline;
- it is a local ACP transport, keeps the active session context in memory, lets
  users select an agent CLI/model, and still asks before agent command execution;
- agent target selection is strict rather than silently falling back to another
  host or WSL environment.

Sources: [Windows Terminal shell integration](https://learn.microsoft.com/en-us/windows/terminal/tutorials/shell-integration),
[Intelligent Terminal repository](https://github.com/microsoft/intelligent-terminal),
[Intelligent Terminal dependency and integration installation](https://github.com/microsoft/intelligent-terminal/blob/main/doc/installing-dependencies.md).

Implication: ztermy cannot promise universal rich semantics without shell startup
cooperation. It should improve on silent first-run mutation by defaulting to an
ephemeral wrapper and offering a separately consented, previewed, reversible
persistent installer. PowerShell/ConPTY is the first rich acceptance target;
remote and nested shells degrade explicitly.

## AI-first agent harnesses: Codex and Claude Code

Codex and Claude Code are not terminal emulators, but their mature agent loops
expose lessons missing from terminal-plus-chat comparisons:

- technical sandbox scope and approval policy are separate controls; keeping
  routine work inside a bounded scope reduces permission fatigue;
- capability-specific allow/deny rules and pre-tool hooks can block destructive
  actions before execution;
- permission grants have explicit lifetimes such as one action, session, local
  settings, project settings, or managed policy;
- tool loops need retry/stopping limits, and a repeated provider event cannot be
  allowed to repeat a completed side effect;
- automatic review can reduce prompts, but it remains a policy decision around an
  already bounded action rather than authority supplied by model text.

Sources: [OpenAI Codex sandbox and approvals](https://learn.chatgpt.com/docs/sandboxing),
[OpenAI agent approvals and security](https://learn.chatgpt.com/docs/agent-approvals-security),
[Claude Code hooks](https://code.claude.com/docs/en/hooks),
[Claude Code sandboxing](https://code.claude.com/docs/en/sandboxing).

Implication: ztermy needs one-writer session ownership, replay-safe dispatch,
pre-execution risk policy, hard turn budgets, and visible grant lifetime. It does
not need to imitate a repository filesystem sandbox, which would not constrain
an already authenticated remote SSH account reliably.

## NetCatty: broad local context and pragmatic compaction

The local reference implementation confirms:

- terminal context can come from the active xterm.js buffer or a hibernation
  snapshot;
- reads are range-based and report total/start/end/has-more metadata plus
  alternate-screen state;
- terminal selections can become explicit AI attachments;
- AI scope can be a terminal or workspace and includes host/session metadata;
- permission modes are `observer`, `confirm`, and `auto`;
- repeated read results and older terminal executions are pruned under context
  pressure, while recent executions and stable permission/session state are
  re-injected after compaction.

Local evidence:

- `D:/tmp/Netcatty/components/terminal/terminalContextBuffer.ts`
- `D:/tmp/Netcatty/domain/terminalContextRead.ts`
- `D:/tmp/Netcatty/components/terminalLayer/useTerminalAiContexts.ts`
- `D:/tmp/Netcatty/infrastructure/ai/shared/toolExecutors.ts`
- `D:/tmp/Netcatty/infrastructure/ai/harness/cattyToolApproval.ts`
- `D:/tmp/Netcatty/infrastructure/ai/harness/staleContextPruner.ts`

Implication: range metadata, explicit selection attachments, provenance, stale-
read pruning, and post-compaction reinjection are worth retaining. ztermy should
improve on the buffer-first approach by preferring command blocks and shell
lifecycle evidence when available. Because the reference is GPL-3.0, these are
behavioral requirements only; ztermy must not port source, names, algorithms, or
file structure from the local tree.

## Wave: widgets as tools

Wave exposes terminal scrollback and line ranges through its `wsh` interface and
lets Wave AI read terminal output alongside file/widget tools. This is a useful
example of defining a stable application capability API that both UI and AI can
consume.

Sources: [Wave AI](https://docs.waveterm.dev/waveai),
[wsh reference](https://docs.waveterm.dev/wsh-reference).

Implication: ztermy tools should call owned application services, not scrape QML
or reach into mutable renderer state.

## Tabby: extensibility, not an AI-native core

Tabby's primary terminal remains plugin-oriented. Its public repository lists an
MCP server plugin that exposes terminal capabilities to external AI clients, but
this is not equivalent to a built-in semantic terminal agent.

Source: [Tabby repository](https://github.com/Eugeny/tabby).

Implication: MCP is a valuable V3 extension boundary, but it should follow a
native typed tool model. Making MCP the internal architecture would force local
terminal actions through an unnecessary protocol hop and complicate ownership.

## Provider/API implications

OpenAI's current official guidance recommends the Responses API for reasoning,
tool calling, and multi-turn workflows. Streaming Responses emits typed events,
including incremental function-call arguments. Ollama uses streamed NDJSON. Qt
already provides asynchronous incremental reads through `QNetworkReply`.

Sources:

- [OpenAI model and Responses guidance](https://developers.openai.com/api/docs/guides/latest-model)
- [OpenAI Responses streaming events](https://platform.openai.com/docs/api-reference/responses-streaming)
- [Ollama streaming](https://docs.ollama.com/api/streaming)
- [Qt `QNetworkReply`](https://doc.qt.io/qt-6/qnetworkreply.html)
- [MCP architecture](https://modelcontextprotocol.io/specification/2025-06-18/architecture)

Implication: use Qt Network and ztermy-owned typed events. Do not add a web
runtime, Node.js, or libcurl solely for V3 AI streaming.

## Evaluation references

Terminal-Bench now ships as the Harbor-hosted continuous benchmark with versioned
datasets, execution environments, oracle validation, and repeated runs. It is a
useful external comparison for long-horizon terminal agency, but its broad Linux
tasks do not replace ztermy's deterministic ConPTY, SSH, shell-integration,
privacy, and UI acceptance matrix.

Sources: [Terminal-Bench repository](https://github.com/harbor-framework/terminal-bench),
[Terminal-Bench paper](https://arxiv.org/abs/2601.11868).

Implication: pin and report a representative external subset, while keeping a
versioned internal corpus with executable state/tool-trace graders and a human
factual rubric for explanations. A release gate must define repetitions and pass
thresholds instead of treating task names as checkboxes.

## Comparison matrix

| Product | Best idea to adopt | Boundary or weakness | ztermy decision |
| --- | --- | --- | --- |
| Warp | Semantic blocks, typed live-command tools, control handoff, fine permissions | Hosted AI brain remains server-side; background attribution and redaction remain imperfect | Reimplement client concepts natively; own providers/orchestration locally |
| VS Code | Authenticated shell lifecycle and explicit integration quality | Injection is not universal, especially nested/remote shells | OSC 133/633-compatible integration with degraded modes |
| Windows/Intelligent Terminal | Native ConPTY shell marks, strict target, ACP transport, error context | Documented integration mutates profiles and supports a limited shell set | Ephemeral default plus explicit reversible installer; PowerShell first |
| Codex/Claude Code | Separate sandbox/policy, grant lifetimes, pre-tool hooks, loop limits | Repository-agent boundaries do not sandbox remote SSH side effects | Typed policy/risk overlay, one writer, deduplication, watchdogs |
| NetCatty | Range-based terminal context, selection attachments, host/workspace tools, pragmatic compaction | Buffer text is less semantic than command blocks | Keep range fallback; prefer semantic blocks |
| Wave | Stable widget/application tool API | Scrollback alone does not solve command attribution | Tools call application services, never QML pixels |
| Tabby | Plugin/MCP ecosystem | MCP plugin is not an AI-native terminal architecture | MCP after native tools, not before |
| Terminal-Bench/Harbor | Versioned executable terminal-agent benchmark | Broad Linux tasks do not cover ztermy UX/Windows/security | Track a pinned subset; internal deterministic suite remains the gate |

## Differentiation opportunity

ztermy can be better for a personal professional SSH workflow by combining:

- Warp-like command semantics without requiring a hosted Warp backend;
- VS Code-quality shell lifecycle verification;
- NetCatty-level SSH/SFTP/telemetry/session context;
- native Windows credential and portable encrypted-vault integration;
- a transparent context preview and local redaction pipeline;
- direct, configurable professional execution rather than one compulsory warning
  policy;
- one provider-independent runtime supporting both local and cloud models.
