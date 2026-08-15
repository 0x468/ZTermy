# V3 implementation traceability

Status: `0.3.0`-`0.3.9` implementation audit in progress; owner/environment
release acceptance tracked separately; `0.3.10` implemented and `0.3.11`
external-Agent integration in progress (`ADR 0091`)

Date: 2026-08-13

## Purpose

This matrix ties every approved V3 delivery slice to representative production
code, executable CTest contracts, and accepted architecture decisions. It is a
traceability aid, not a substitute for executed test output or the manual
matrix. The authoritative execution record remains
`V3_0_3_0_RC_ACCEPTANCE.md`.

The test count is recorded by the final rebuilt candidate rather than frozen in
this living matrix. The rows below name the
focused contracts that prove each V3 boundary; inherited terminal, SSH, SFTP,
credential, persistence, packaging, and real-window tests remain required in
addition.

## 0.3.0 — semantic assistant foundation

| Approved boundary | Representative implementation | Focused CTest contracts | Decisions |
| --- | --- | --- | --- |
| Semantic command lifecycle and honest output coverage | `src/domain/terminal/ShellIntegrationDecoder.*`, `CommandBlockAssembler.*`, `CommandBlockStore.*`, `SemanticTerminalObserver.*` | `command-block-store`, `shell-integration-decoder`, `command-block-assembler`, `semantic-terminal-observer` | ADR 0054, 0055 |
| Provider-neutral request, streaming, retry, usage, and cancellation | `src/domain/ai/AiProviderTypes.h`, `AiProviderRetryPolicy.*`; `src/infrastructure/ai/ProviderRequestFactory.*`, `ProviderStreamMapper.*`, `ProviderHttpClient.*`; `src/application/ai/AiTurnRunner.*` | `ai-stream-parser`, `provider-stream-mapper`, `provider-request-factory`, `provider-http-client`, `ai-provider-retry-policy`, `ai-usage-reporting`, `ai-turn-runner` | ADR 0054, 0057, 0077 |
| Bounded, redacted, inspectable context and read tools | `src/domain/ai/AiContextBroker.*`, `AiReadTools.*`, `AiContextRedactor.*`, `AiContextSerializer.*`; `src/application/ai/AiReadToolDispatcher.*` | `ai-context-broker`, `ai-read-tools`, `ai-context-redactor`, `ai-context-serializer`, `ai-read-tool-dispatcher` | ADR 0054, 0059 |
| Native assistant surface, explicit Insert/Run, protected copy | `src/application/AppController.*`, `src/application/ai/AiConversationModel.*`, `src/ui/qml/AiAssistantPane.qml`, `src/platform/windows/WindowsProtectedClipboard.*` | `app-controller`, `ai-conversation-model`, `windows-protected-clipboard`, `qml-native-window-smoke` | ADR 0058, 0078 |

## 0.3.1 — executable terminal agent

| Approved boundary | Representative implementation | Focused CTest contracts | Decisions |
| --- | --- | --- | --- |
| Typed terminal write/read/wait/interrupt/control tools | `src/application/ai/AiActionToolDispatcher.*`, `AiWaitCommandTool.*`; `src/domain/ai/AiCommandTracker.*` | `ai-action-tool-dispatcher`, `ai-wait-command-tool`, `ai-command-tracker` | ADR 0056, 0062 |
| At-most-once dispatch, permission precedence, risk overlay, budgets, one writer | `src/domain/ai/AiToolDispatchLedger.*`, `AiPermissionPolicy.*`, `AiAgentGuard.*` | `ai-tool-dispatch-ledger`, `ai-permission-policy`, `ai-agent-guard` | ADR 0056 |
| Metadata-only audit and encrypted opt-in conversation history, including complete bounded Agent presentation snapshots | `src/application/ai/AiActivityModel.*`, `AiConversationHistoryModel.*`, `AiConversationModel.*`; `src/infrastructure/ai/AiConversationStore.*` | `ai-activity-model`, `ai-conversation-store`, `ai-conversation-history-model`, `ai-conversation-model`, `app-controller` | ADR 0060, 0061 |

## 0.3.2 — SSH operations intelligence

| Approved boundary | Representative implementation | Focused CTest contracts | Decisions |
| --- | --- | --- | --- |
| Bounded, cancellable SFTP list/read tools | `src/application/ai/AiSftpListTool.*`, `AiSftpReadTool.*` | `ai-sftp-list-tool`, `ai-sftp-read-tool` | ADR 0063, 0066 |
| Approved SFTP mutations through the existing transfer graph | `src/application/ai/AiActionToolDispatcher.*`, `src/application/transfer/*` | `ai-action-tool-dispatcher`, `transfer-queue`, `transfer-batch`, `transfer-manager`, `transfer-recovery-store` | ADR 0051, 0068 |
| Telemetry, scripts, notes, history, current-terminal snapshots, and owned runbooks | `src/domain/ai/AiReadTools.*`; `src/application/ai/AiNoteReadTool.*`; `src/application/AppController.*` | `ai-read-tools`, `ai-note-read-tool`, `remote-telemetry`, `script-store`, `note-store`, `workspace-state-store`, `ai-action-tool-dispatcher` | ADR 0064, 0067, 0086 |

## 0.3.3 — interactive terminal control

| Approved boundary | Representative implementation | Focused CTest contracts | Decisions |
| --- | --- | --- | --- |
| Bounded full/incremental frame observation and deterministic wait conditions | `src/domain/ai/AiTerminalFrameTracker.*`; `src/application/ai/AiTerminalFrameTool.*` | `ai-terminal-frame-tracker`, `ai-terminal-frame-tool` | ADR 0069, 0070 |
| Explicit user/agent takeover and one-writer ownership | `src/domain/ai/AiAgentGuard.*`; `src/application/ai/AiActionToolDispatcher.*` | `ai-agent-guard`, `ai-action-tool-dispatcher` | ADR 0056, 0070 |
| Honest rich/basic/none degradation across shell and terminal modes | `src/domain/ai/AiTerminalCapabilityAdapter.*`, `src/domain/terminal/SemanticTerminalObserver.*` | `ai-terminal-capability-adapter`, `semantic-terminal-observer`, `local-terminal-session`, `ssh-terminal-session` | ADR 0055, 0071 |

## 0.3.4 — MCP, evaluation, privacy, and quality closure

| Approved boundary | Representative implementation | Focused CTest contracts | Decisions |
| --- | --- | --- | --- |
| MCP stdio/JSON-RPC transport, namespace isolation, trust and schema review | `src/infrastructure/ai/McpJsonRpcProtocol.*`, `McpStdioClient.*`, `McpServerStore.*`; `src/domain/ai/McpToolRegistry.*`; `src/application/ai/McpRuntimeManager.*` | `mcp-json-rpc-protocol`, `mcp-stdio-client`, `mcp-server-store`, `mcp-tool-registry`, `mcp-runtime-manager`, `ai-mcp-lifecycle-stress` | ADR 0073, 0074 |
| Versioned provider-independent evaluation replay | `src/infrastructure/ai/AiEvaluationHarness.*`, `tests/fixtures/ai-eval/v1/*` | `ai-evaluation-harness` | ADR 0072 |
| Privacy diagnostics and provider wire contracts | `src/application/ai/AiPrivacyDiagnostics.*`, provider adapter files above | `ai-privacy-diagnostics`, `provider-http-client`, `provider-request-factory`, `provider-stream-mapper` | ADR 0075, 0077 |
| Concurrency, responsive accessibility, translation, package, and release evidence | `scripts/run_ai_concurrency_soak.ps1`, `scripts/run_terminal_stability_soak.ps1`, `scripts/verify_v3_release_candidate.ps1`; real-window/package CMake gates | `ai-mcp-lifecycle-stress`, `qml-native-window-smoke`, `translation-catalog`, `windows-executable-metadata`, `branding-assets` plus the opt-in duration drivers | ADR 0076, 0078 and inherited ADR 0014–0016 |

## 0.3.5 — agent conversation repair

| Approved boundary | Representative implementation | Focused CTest contracts | Decisions |
| --- | --- | --- | --- |
| Completed command results return normalized retained output and explicit completeness instead of forcing the model to infer from the visible viewport | `src/domain/ai/AiCommandTracker.*`, `src/application/ai/AiWaitCommandTool.*`, `src/domain/ai/AiContextBroker.*` | `ai-command-tracker`, `ai-wait-command-tool`, `ai-context-broker` | ADR 0080 |
| Cancellation is a terminal, retryable conversation state and cannot be overwritten by the synchronous cancel callback | `src/application/AppController.*`, `src/application/ai/AiConversationModel.*`, `AiTurnRunner.*` | `app-controller`, `ai-conversation-model`, `ai-turn-runner` | ADR 0080 |
| Assistant Markdown, raw-copy fidelity, provider-exposed reasoning, and opt-in wire trace | `src/ui/qml/AiAssistantPane.qml`, `src/infrastructure/ai/ProviderRequestFactory.*`, `ProviderHttpClient.*`, `src/application/AppController.*` | `provider-request-factory`, `provider-http-client`, `ai-conversation-model`, `qml-native-window-smoke` | ADR 0080 |
| Ordinary terminal typing and agent turns share an invisible serialized PTY lease; the next AI turn resumes agent ownership without a daily takeover control | `src/application/AppController.*`, `src/domain/ai/AiAgentGuard.*`, `src/ui/qml/AiAssistantPane.qml` | `app-controller`, `ai-agent-guard`, `ai-action-tool-dispatcher` | ADR 0080 |

## 0.3.6 — modes and reusable rules

The five user-facing modes (`read-only`, `ask`, `edit`, `auto`, and `yolo`),
legacy-settings migration, and the in-panel mode selector are implemented and
covered by `application-settings`, `ai-permission-policy`,
`ai-action-tool-dispatcher`, and `app-controller`.

| Approved boundary | Representative implementation | Focused CTest contracts | Decisions |
| --- | --- | --- | --- |
| Typed capability rules, exact/prefix/glob/regex/all matching, scope, precedence, and once consumption | `src/domain/ai/AiPermissionPolicy.*` | `ai-permission-policy` | ADR 0080 |
| Session-only ephemeral rules plus bounded Profile/global persistence and backup recovery | `src/infrastructure/ai/AiPermissionRuleStore.*`, `src/application/AppController.*` | `ai-permission-rule-store`, `app-controller` | ADR 0080 |
| Rule overrides across terminal, PTY, interrupt, runbook, and SFTP actions | `src/application/ai/AiActionToolDispatcher.*` | `ai-action-tool-dispatcher` | ADR 0080 |
| Compact approval-card scope/matcher editor and Settings view/edit/toggle/revoke manager | `src/ui/qml/AiAssistantPane.qml`, `src/ui/qml/SettingsPane.qml` | QML compilation, translation gate, real-window smoke | ADR 0078, 0080 |

## 0.3.7 — reasoning and conversation presentation

| Approved boundary | Representative implementation | Focused CTest contracts | Decisions |
| --- | --- | --- | --- |
| Provider-exposed reasoning controls and retained summaries without claiming hidden chain-of-thought; encrypted restore preserves the same reasoning/tool/usage presentation | `src/infrastructure/ai/ProviderRequestFactory.*`, `ProviderStreamMapper.*`; `src/application/ai/AiConversationModel.*`; `src/infrastructure/ai/AiConversationStore.*` | `provider-request-factory`, `provider-stream-mapper`, `ai-conversation-model`, `ai-conversation-store`, `app-controller` | ADR 0061, 0081 |
| Rich Markdown rendering, raw source copy, native tool cards, responsive cancel/retry | `src/ui/qml/AiAssistantPane.qml`, `src/application/AppController.*` | `qml-native-window-smoke`, `app-controller`, `ai-turn-runner` | ADR 0078, 0081 |

## 0.3.8 — autonomous Agent closure

| Approved boundary | Representative implementation | Focused CTest contracts | Decisions |
| --- | --- | --- | --- |
| Mode-to-terminal end-to-end execution through real ConPTY/PowerShell, semantic command blocks, retained result, replay, long-command user input, and interactive PTY response | `tests/ai_agent_scenario_tests.cpp` plus the production dispatcher/session/observer/tracker path | `ai-agent-scenario` | ADR 0083 |
| Agent and user input through a real key-authenticated SSH PTY, including long and interactive commands | `tests/ai_agent_real_host_tests.cpp` plus `SshTerminalSession` and the production action dispatcher | `ai-agent-real-host` (environment-gated) | ADR 0083 |
| Edit/Auto/YOLO consistency for terminal, runbook, SFTP, and reviewed MCP tools | `src/application/ai/AiActionToolDispatcher.*`, `src/application/AppController.*` | `ai-action-tool-dispatcher`, `app-controller`, `mcp-runtime-manager` | ADR 0080, 0082 |
| Expanded mixed Agent/MCP duration set rejects stale reports that predate the end-to-end scenario | `scripts/run_ai_concurrency_soak.ps1`, `scripts/verify_v3_release_candidate.ps1` | dynamic Debug and static Release schema-2 developer runs plus formal duration report before final RC | ADR 0076, 0083 |

## 0.3.9 — NetCatty-class conversation surface

| Approved boundary | Representative implementation | Focused contracts | Decisions |
| --- | --- | --- | --- |
| Conversation-first header, composer footer, current model/mode selection, and Markdown export | `src/ui/qml/AiAssistantPane.qml`, `src/application/AppController.*` | `app-controller`, QML compilation, translation gate | Product review 2026-08-13 |
| Sticky streaming, Return to latest, reasoning lifecycle, and coalesced Markdown layout | `src/ui/qml/AiAssistantPane.qml`, `MarkdownMessage.qml` | QML compilation and owner real-window acceptance | ADR 0078, 0081 |
| Local table/code overflow and per-code raw copy | `src/ui/qml/MarkdownMessage.qml`, `src/platform/windows/WindowsProtectedClipboard.*` | `windows-protected-clipboard`, owner real-window acceptance | ADR 0078 |
| Explicit recent commands with honest non-semantic fallback | `src/application/AppController.*`, `src/domain/terminal/SemanticTerminalObserver.*` | `app-controller`, `semantic-terminal-observer` | ADR 0055, product review 2026-08-13 |
| Keyboard-first built-in slash commands | `src/ui/qml/AiAssistantPane.qml`, existing `AppController` AI actions | QML compilation, translation gate, owner keyboard acceptance | Product review 2026-08-13 |
| Consecutive tool calls grouped per assistant turn, expandable bounded arguments/results, and inert restored snapshots | `src/application/ai/AiConversationModel.*`, `src/infrastructure/ai/AiConversationStore.*`, `src/ui/qml/AiAssistantPane.qml` | `ai-conversation-model`, `ai-conversation-store`, `qml-native-window-smoke`, `ui-layout-runtime-smoke` | ADR 0061, Netcatty comparison 2026-08-15 |
| Compact one-row header with low-frequency conversation actions in overflow | `src/ui/qml/AiAssistantPane.qml`, `src/main.cpp` | `ui-layout-runtime-smoke` accessibility and 260 px geometry contracts | Netcatty comparison 2026-08-15 |
| Drag local images/text onto the owning terminal composer without auto-send or cross-terminal routing | `src/ui/qml/AiAssistantPane.qml`, existing `AppController` asynchronous attachment loaders | `ui-layout-runtime-smoke` 260 px geometry contract, owner Explorer drag/drop acceptance | ADR 0087, 0089 |

## 0.3.10 — current-terminal native tool contract

| Approved boundary | Representative implementation | Focused contracts | Decisions |
| --- | --- | --- | --- |
| One sidebar exposes only its owning terminal; no session discovery or provider-selected routing | `src/application/AppController.*`, `src/application/ai/AiReadToolDispatcher.*`, `AiActionToolDispatcher.*` | `ai-read-tool-dispatcher`, `ai-action-tool-dispatcher`, `app-controller` | ADR 0086 |
| Native schemas/results/context omit internal tab identity and reconnect generation | `src/application/ai/Ai*Sftp*Tool.*`, `AiNoteReadTool.*`, `AiTerminalFrameTool.*`, `AiWaitCommandTool.*`; `src/domain/ai/AiContextSerializer.*` | `ai-sftp-list-tool`, `ai-sftp-read-tool`, `ai-note-read-tool`, `ai-terminal-frame-tool`, `ai-wait-command-tool`, `ai-context-serializer` | ADR 0086 |
| Tab focus changes cannot retarget a turn; reconnect/closure invalidates live work | `src/application/AppController.*` and host-injected `AiSessionTarget` values | `app-controller`, Agent scenario tests, owner concurrency acceptance | ADR 0086 |
| Explicit images remain in the owning draft and use native multimodal provider payloads | `src/application/AppController.*`, `AiConversationModel.*`, `ProviderRequestFactory.*`, `AiAssistantPane.qml` | `provider-request-factory`, `ai-conversation-model`, `ai-context-compactor`, `ai-trace-sanitizer`, `app-controller`, owner multimodal acceptance | ADR 0089 |
| Provider-native web search remains current-terminal scoped and exposes structured source provenance | `src/application/AppController.*`, `AiConversationModel.*`, `AiConversationStore.*`, `ProviderRequestFactory.*`, `ProviderStreamMapper.*`, `AiAssistantPane.qml` | `provider-request-factory`, `provider-stream-mapper`, `ai-conversation-model`, `ai-conversation-store`, `app-controller`, owner/provider acceptance | ADR 0090 |

## Evidence classification

- **Implementation coverage:** every approved `0.3.0`–`0.3.8` boundary above
  has production code and at least one focused executable or real-window
  contract.
- **Automated candidate evidence:** the exact builds, test totals, static
  analysis, real-window gates, real-host key-only runs, and package hashes are
  recorded in `V3_0_3_0_RC_ACCEPTANCE.md`.
- **Duration evidence:** the final schema-2 Agent/MCP report covers the expanded
  `0.3.8` set: 182 complete iterations in 7232.8 seconds with zero failures.
  The retained AI-idle terminal report covers 28800.1 seconds with zero
  failures. Both reports pass the unified RC verifier with the final rebuilt
  release bundle.
- **Current developer evidence:** the `0.3.9` dynamic Debug candidate passes
  105/105 tests, the static Release application builds, QML/C++ format and lint
  pass, and the expanded translation catalog passes 1619/1619. The prior formal
  `0.3.8` evidence retains its 20-run scenario, 224-unit clang-tidy, static
  suite, package, and eight-gate real-window results; those full release gates
  are rerun for the next release candidate rather than inferred from this UI
  iteration.
- **Current real-host evidence:** `ai-agent-real-host` passed against the
  owner-provided key-authenticated Linux SSH host in 1.681 seconds, covering
  automatic execution, user input queued behind a long command, and
  interactive `write_to_pty` response.
- **Owner/environment evidence:** live-provider quality, manual AI UX, password
  authentication, MSI interaction, and the previous supported Windows 11 build
  remain explicitly unchecked. They cannot be inferred from the focused tests
  above.
