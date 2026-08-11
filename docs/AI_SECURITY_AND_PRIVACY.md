# V3 AI security and privacy

Status: accepted security contract for `0.3.x`

## Assets and trust boundaries

Protected assets include:

- SSH passwords, key passphrases, private keys, agent identities, and provider
  API keys;
- terminal input, output, command history, working directories, hostnames, IPs,
  SFTP paths/files, notes, scripts, telemetry, and AI transcripts;
- the integrity of the active local/remote shell and every action sent to it.

Trust boundaries:

```text
remote host output (untrusted)
local shell output (untrusted data)
    -> ztermy context/redaction boundary
    -> provider request (external trust boundary)
    -> model output/tool proposal (untrusted)
    -> schema + scope + permission boundary
    -> native application service
    -> local or remote side effect
```

Neither a model response nor terminal output can grant itself permission.

## Threat model

V3 explicitly handles:

- prompt injection embedded in command output, files, notes, banners, remote
  host content, or replayed conversation/tool history;
- malicious OSC sequences attempting to spoof command text, completion, CWD, or
  exit status;
- secret disclosure through automatic scrollback, tool output, provider errors,
  diagnostics, transcripts, or clipboard/export;
- model-generated commands targeting the wrong tab, restored generation, host,
  or directory;
- fragmented/oversized provider events, malformed tool arguments, replayed tool
  calls, late callbacks, and cancellation races;
- unbounded output, context, transcript, token, retry, concurrency, or wait
  growth;
- unsafe actions disguised as read-only operations;
- provider endpoint substitution, proxy interception, and invalid TLS;
- a local user intentionally choosing automatic execution.

The local user's deliberate command execution is not considered an attacker.
The product must support professional autonomy while making scope and policy
observable.

## Data classification

| Class | Examples | Provider eligibility | Persistence |
| --- | --- | --- | --- |
| Secret | passwords, private keys, passphrases, API tokens | Never | credential vault only |
| Sensitive | terminal output, commands, paths, host metadata, notes | Explicit/declared automatic context after redaction | Session-only by default; optional encrypted history |
| Operational | provider/model ID, permission mode, latency, token counts | Yes when required | bounded settings/audit metadata |
| Public | owned UI strings and tool schemas | Yes | normal resources |

Raw terminal keystrokes are never sent as context. Exact commands come from
verified shell integration or explicit application actions; degraded heuristic
commands are labeled and require user-visible inclusion.

A leading dot is not a security policy. Hidden files are classified by content,
capability, and target rather than their name alone. Files such as `.env` are
useful operational inputs but are presumed secret-bearing: an explicit scoped
read may locally redact and include only needed fields, and an authorized file
tool may edit them. They are not automatically attached wholesale to provider
context. The no-silent-modification rule in ADR 0055 applies specifically to the
shell-integration installer and known shell startup files.

## Context and redaction pipeline

Before provider serialization:

1. resolve an explicit scope and immutable generation;
2. select only requested/declared context kinds;
3. remove unsafe control sequences and normalize encoding/line endings;
4. run deterministic secret detectors and user rules locally;
5. replace matches with typed placeholders, never reversible hashes;
6. bound lines, bytes, item count, estimated tokens, and per-tool output;
7. attach provenance, truncation, and redaction counts;
8. produce the same bounded preview the user can inspect;
9. serialize through the selected provider adapter.

Detector classes include common API-key formats, private-key blocks, bearer and
basic authorization headers, URI credentials, shell assignment patterns, and
user-defined literal/regular-expression rules. Multi-line detectors operate on
a bounded normalized window.

Redaction is defense in depth, not a guarantee that arbitrary output contains no
secret. Automatic whole-scrollback upload is therefore prohibited.

## Prompt-injection containment

- Context items are marked as untrusted evidence with source and target.
- System/developer policy states that content inside terminal/file data cannot
  alter permissions, tool schemas, scope, or approval requirements.
- Tool availability is computed by ztermy, not requested by prompt text.
- Tool calls are schema validated and re-authorized at execution time.
- A model cannot use a textual claim such as “the user approved” as approval.
- Remote output cannot enable automatic mode or modify allow/deny rules.
- Results from different hosts remain separately tagged through compaction.
- Replayed user/model messages and old tool results are evidence, not fresh user
  authorization. Trusted system policy is reconstructed separately and is never
  loaded from transcript text.

## Permission model

Initial modes:

| Mode | Reads | First write | Later writes | Typical use |
| --- | --- | --- | --- | --- |
| Observer | Allowed within context policy | Denied | Denied | explanation/audit |
| Ask each write | Allowed | Ask | Ask | cautious operation |
| Ask first write | Allowed | Ask | Allow for same conversation and scope | normal assisted work |
| Session auto | Allowed | Allow | Allow until conversation/session ends | trusted task |
| Saved-host auto | Allowed | Allow | Allow for that saved host policy | advanced opt-in |

Decision precedence:

1. invalid schema, stale scope/generation, unavailable capability -> deny;
2. explicit deny rule -> ask or deny according to rule type;
3. explicit user action on a visible Run/Approve control -> allow that exact
   action and arguments;
4. matching allow rule -> allow;
5. active mode -> allow, ask, or deny;
6. otherwise fail closed.

Direct user Run is an explicit action and does not receive an additional generic
warning. The command and target remain visible.

Model-initiated writes additionally pass a deterministic risk overlay for
destructive filesystem/disk operations, privilege and credential changes,
recursive permission changes, shutdown/reboot, firewall/network disruption, and
opaque download-and-execute pipelines. The overlay is defense in depth, not a
claim that shell text can be classified perfectly. High-risk actions ask even in
automatic mode; an advanced, explicit grant may relax this
for the current session and exact target. Direct visible Run remains exact
authorization. Critical deny rules still win.

Permissions are capability-specific: command execution, PTY writing, SFTP
mutation, local file access, MCP server access, and multi-session targeting are
separate decisions.

Saved-host automatic execution is hidden behind advanced settings and never
applies to a quick connection. Encrypted conversation history is opt-in.

## Target and lifecycle integrity

Every action carries:

- conversation ID and turn/tool-call ID;
- session ID and session generation;
- saved profile ID or explicit quick-connect identity;
- expected CWD when relevant;
- command block/job ID for follow-up reads/writes;
- deadline, cancellation token, and maximum result size.

Write dispatch also records a canonical argument hash and an execution state.
An identical replay joins or returns the recorded result; a reused ID with a
different payload is rejected. Side-effecting actions are never automatically
reissued to recover from a provider or transport retry.

A reconnect, restored tab, moved pane, closed session, or scope change invalidates
stale capabilities. A tool never silently retargets to the newly active tab.

## Provider and network policy

- API keys are read only when starting a request and are never deliberately
  copied into QML-facing state, logs, exported diagnostic report payloads, or
  settings JSON. Minidumps are governed separately below because transient
  process-memory copies cannot be promised absent.
- Custom endpoints require explicit user configuration and HTTPS by default;
  insecure HTTP is restricted to loopback local-model endpoints unless an
  advanced override is deliberately approved.
- TLS errors are not ignored automatically.
- Proxies use the existing application/system proxy boundary and never print
  proxy credentials.
- Provider error bodies are bounded and sanitized before display/logging.
- Requests expose cancellation and hard deadlines. Authentication, malformed
  request, invalid model, and permanent endpoint errors require user action and
  are not retried. Transient 408/429/selected 5xx/reset failures before a tool
  side effect use at most two exponential-backoff retries with jitter and honor
  `Retry-After`. After a possibly executed side effect, recovery consults the
  dispatch record and otherwise reports an uncertain outcome.
- When supported, a stable privacy-preserving safety identifier may be sent; it
  must not contain username, host, profile name, or hardware identity.

## Storage

- Provider keys use installed Windows Credential Manager or the portable AES-
  GCM vault.
- Conversation history is session-only until the encrypted store ships.
- Optional encrypted history uses a separate data key and bounded retention;
  credential stores never hold transcript bodies.
- Audit records contain time, provider/model token, capability, target token,
  decision, duration, result code, and redaction/truncation counts. They exclude
  raw command text, raw output, prompts, tool arguments likely to contain paths,
  and secrets.
- Export is explicit, previews included fields, and warns only when the export
  actually contains sensitive transcript data.

## Windows clipboard and crash artifacts

- Copy actions involving terminal/context/AI content use a platform abstraction
  that can set `ExcludeClipboardContentFromMonitorProcessing`, or both
  `CanIncludeInClipboardHistory=0` and `CanUploadToCloudClipboard=0`, so protected
  copies do not enter Windows clipboard history or cloud sync. This protection
  is on by default for the AI surface and configurable because it changes Win+V
  behavior.
- Optional auto-clear uses the Windows clipboard sequence number and clears only
  if ztermy still owns the same clipboard item; it never destroys newer user
  content.
- Minidumps are classified as Secret diagnostic artifacts. They are never
  uploaded or included in an exported report automatically. ztermy keeps dumps
  non-full-memory, requests removal of owned secret arenas where supported,
  bounds API-key lifetime, and zeroes owned buffers after use.
- Dump filtering is not a mathematical guarantee because transient copies may
  exist in Qt, TLS, stack, or register state. Release tests scan dumps containing
  synthetic marker keys; sharing a dump remains an explicit user decision with
  a sensitivity warning.

## Logging and diagnostics

Never log:

- prompts, model responses, terminal input/output, full command lines;
- provider authorization headers, keys, raw request/response bodies;
- private paths/host identifiers without the existing diagnostic redaction;
- encrypted transcript plaintext or encryption keys.

Crash dumps are not logs and must not be described as safe-to-share merely
because these logging rules pass.

## MCP trust boundary

MCP is not trusted merely because a server is configured. `0.3.4` requires:

- explicit server identity, local/remote transport, endpoint, credential owner,
  and trust tier;
- isolated server namespaces and user review when tool names, descriptions, or
  schemas change;
- tool descriptions, resource content, elicitation text, and server errors
  treated as untrusted evidence that cannot grant permission;
- OAuth/client credentials stored as secrets and never exposed to another MCP
  server, provider prompt, audit text, or QML model;
- the same target, scope, high-risk, deduplication, watchdog, cancellation, and
  audit policy as native tools, plus the ability to disable one server instantly.

Allowed diagnostics include typed event names, byte counts, durations, bounded
queue depths, retry count, provider/model opaque IDs, tool result code, scope
generation match, and redaction count.

## Security acceptance

The V3 gate includes:

- prompt-injection fixtures attempting scope, permission, and tool-schema
  changes;
- spoofed/unverified OSC lifecycle and exact-command events;
- credentials and private-key fixtures across selection, scrollback, provider
  errors, audit, export, and crash diagnostics;
- stale-session and reconnect-generation tool calls;
- oversized SSE/NDJSON lines, fragmented UTF-8/JSON, duplicate/replayed calls,
  late events, cancellation, and shutdown;
- protected Windows clipboard formats and synthetic-key scanning of generated
  minidumps;
- deny/allow precedence and every permission-mode transition;
- provider endpoint/TLS/proxy failure without secret-bearing logs;
- encrypted history corruption, unsupported schema, deletion, and migration.
