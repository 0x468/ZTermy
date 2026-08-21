# V3 0.3.0 release-candidate acceptance

Status: final-source automated release candidate; owner/provider and
environment-dependent interaction checks remain

Date: 2026-08-21

Identity:

- version: `0.3.0`;
- codename: **糸**;
- verse: **「剪不断，理还乱，是离愁」**;
- supported release platform: Windows 11 x64;
- candidate executable source: `1e12968`.

This document records the final-source automated evidence for that exact
package. It includes the mainstream Agent conversation, five-mode/rule model,
Markdown and reasoning presentation, native ChatGPT subscription transport,
local and real-SSH end-to-end scenarios, and the expanded schema-2 Agent/MCP
duration gate. It does not convert the unchecked owner/provider matrix below
into a pass.

## Environment

- Microsoft Windows 11 Pro 10.0.26200, x64;
- Qt 6.8.3 static Release and official Qt 6.8.3 dynamic Debug;
- Microsoft Visual Studio 2022 17.14.38, MSVC x64;
- CMake 4.4.0 and Ninja 1.13.1;
- PowerShell 7.6.4;
- clang-format/clang-tidy 22.1.6.

## Automated evidence

The following gates completed with exit code zero:

1. Dynamic Debug configure/build and 116/116 CTest tests. The final-source run
   took 131.94 seconds.
2. Static Release configure/build and 116/116 CTest tests. The final-source run
   took 155.42 seconds without test retry. Both configurations include the 32-cycle MCP process
   lifecycle stress and the deterministic local Agent scenario.
3. C++ format, QML format, QML lint, translation, branding, executable metadata,
   and package contracts.
4. clang-tidy with warnings as errors over all 255 C++ translation units in the
   static compilation database.
5. The inherited 30-second local-terminal interaction gate and its 16-test
   persistence/lifecycle core.
6. Eight serial real-window gates: Windows work area and native caption,
   appearance, resize/hit testing, DPI 100/125/150/200, responsive AI layout and
   accessibility, keyboard navigation, terminal rendering, and lifecycle
   shutdown.
7. Provider-independent schema-2 Agent/MCP concurrency soak:
   - dynamic Debug and static Release developer runs each completed one full
     iteration with zero failures;
   - static Release formal duration run completed 182 iterations in 7232.8
     seconds with zero failures. The content-free report spans 2026-08-12
     21:27:30Z through 23:28:03Z, belongs to the static Release build directory,
     and contains the exact 11-test approved set.
8. WiX generation, decompilation, and structural inspection. WiX ICE validation
   was explicitly skipped for this rebuild because the local Windows Installer
   service returned WIX0217/exit 217 before running any ICE rule. Structural
   inspection still passed the per-user LocalAppData, executable identity,
   product icon, Start-menu shortcut, same-version upgrade, and uninstall-folder
   contracts.
9. Final portable archive extraction, `portable.flag` inspection, and packaged
   executable lifecycle smoke. The executable extracted from the checksummed
   final ZIP additionally passed the work-area/caption, appearance,
   resize/hit-test, responsive layout, keyboard, large-output terminal render,
   and lifecycle gates; the packaged binary passed the DPI matrix at
   100/125/150/200 percent and the integrated real-host SSH/SFTP UI smoke.
10. The pinned key-only LAN test host completed 100 authenticated
    connect/disconnect cycles in one `SshTerminalSession` with no failure. A
    second focused real-host run passed explicit host-key confirmation, bounded
    shell-history and telemetry reads, authentication rejection classification,
    remote close, and the 16 ms input-queue P95 gate. The test used only the
    owner-approved host identity, public fingerprint, username, and private-key
    path; no password, passphrase, key content, or terminal command entered the
    command line or report.
    The final Agent-specific real-host gate additionally passed automatic
    command execution, user input queued behind a long remote command, and an
    interactive remote read answered through `write_to_pty`. The integrated
    real-window SSH/SFTP UI smoke also passed against the same pinned host.
11. The formal AI-idle local-terminal stability gate completed 28800.148
    seconds with zero failures. The content-free report spans 2026-08-12
    15:45:37+08:00 through 23:45:38+08:00, records the static Release build
    directory, and passes the unified release-candidate evidence verifier.
12. The final-source protected clipboard contract completed 100 consecutive
    Release repetitions after adding bounded Win32 clipboard contention retry.
    The same contract passed in the final Debug and Release 116-test suites.
13. ChatGPT subscription tests cover PKCE, loopback callback success, state
    rejection, cancellation, native device-code request/pending-poll/token
    exchange, cancellation before polling, token refresh without rotation,
    concurrent refresh rejection, idempotent typed cancellation, typed 429
    recovery, auth-aware model visibility and capability parsing, allowance parsing, credential
    persistence, and provider error recovery. A controller-level fake HTTP proxy
    also proves that model discovery uses the configured AI-only custom proxy.
    The background model-refresh controller test passed 20 consecutive Static
    Release repetitions before both final full suites. The signed-out ChatGPT
    account row and its Chinese device-code entry were also inspected in the
    exact Debug executable at full width and approximately 793 px window width.
    The final static Release then discovered the ChatGPT-only
    `gpt-5.3-codex-spark` entry (while excluding the hidden `gpt-reserve`
    entry), accepted optional/defaulted terminal tool schemas, completed a
    `read_terminal` call, replayed its typed result without
    `previous_response_id`, and returned `PowerShell`. The same executable
    passed all 116 static Release tests after this repair.

The formal soak reports are generated at
`build/msvc-static-release/ai-concurrency-soak-2h-schema2.json` and
`build/msvc-static-release/terminal-stability-soak-8h.json`. They contain test
names, timings, iteration counts or requested duration, and failures only; they
contain no terminal or AI content.

## Release handoff

Authoritative directory:

```text
build/msvc-static-release/package/release/ztermy-0.3.0-windows-x64
```

It contains exactly:

- `ztermy-0.3.0-windows-x64-portable.zip` — SHA-256
  `cf6ac3ed65ee5921c3e340c236214c596b0d1f365f872e019ee67e48faf1a743`;
- `ztermy-0.3.0-windows-x64.msi` — SHA-256
  `d9649a1fef237c5c3cf65b201bf615e8d20bb9b4f33e664bb6f5f4bcee331554`;
- `SHA256SUMS.txt`;
- `release-manifest.json`.

The manifest and checksum file are authoritative if packaging is run again,
because MSI and ZIP container metadata can change artifact hashes without a
source change.

This final bundle was rebuilt and verified on 2026-08-21. WiX ICE validation
could not access the local Windows Installer service and was explicitly skipped;
structural decompilation, the packaged executable metadata and native-window
smoke, and the unified RC verifier all passed. The prior owner-approved sandbox
install/uninstall acceptance remains recorded, while this rebuild's ICE exception
is not represented as a pass.

## Owner acceptance — exact checks

Use a disposable provider key and test host. Do not paste either into an issue,
diagnostic export, or this document.

### 1. Provider settings and request privacy

1. Open Settings > AI and add OpenAI Responses, Ollama, and one compatible
   endpoint as available. Save a cloud key in the selected credential vault.
2. Close and reopen Settings and restart ztermy.
3. Exercise an invalid key, offline endpoint, TLS failure, 429 response, and a
   valid request.
4. Select ChatGPT, complete system-browser sign-in, and verify that model
   discovery and plan usage refresh automatically after restart, terminal
   creation, and first AI-panel open.
5. Sign out, use the device-code fallback, and verify the visible short code,
   copy/open actions, cancellation, and convergence on the same signed-in state.
6. Complete an ordinary subscription response and one harmless terminal tool
   call, then exercise cancel, retry, revoked authorization, sign-in again, and
   sign-out.
7. Exercise system, direct, custom HTTP, and custom SOCKS5 AI proxy modes while
   an SSH session remains connected. Export ordinary application diagnostics.

Expected:

- keys remain masked and survive only in the selected vault;
- errors distinguish authentication, configuration, transient, and offline
  causes without exposing request content or credentials;
- ChatGPT subscription model discovery, allowance windows, streaming responses,
  tool continuation, token refresh, and sign-out behave as described in
  `V3_OWNER_ACCEPTANCE_ZH_CN.md`;
- an expired or revoked subscription asks for ChatGPT sign-in again, not an API
  key, and a network retry never repeats a completed tool call;
- AI proxy changes affect provider traffic only; SSH, SFTP, and forwarding are
  unchanged;
- diagnostics contain no API key, OAuth token, authorization code, terminal
  input, or unredacted secret-bearing command;
- the last selected provider and model are restored.

### 2. Semantic assistant and context control

1. In local PowerShell, run a successful command and a failing command. Open the
   AI panel and use Explain last failure.
2. Select mixed ASCII, CJK, emoji, and a long/truncated region and attach it.
3. Inspect the context preview; remove one item, pin another, and send.
4. Generate a harmless command, use Insert, then use the visible Run action.
5. Cancel one streaming response, retry it, and close the terminal tab while a
   response is active.

Expected:

- the failure explanation shows capability and evidence quality and never
  claims an exact exit status when it is unavailable;
- context provenance, target, bounds, redaction, remove, and pin are visible;
- Insert does not execute; explicit Run executes exactly once without a second
  warning;
- cancellation and tab close produce no late text, late action, hang, or secret
  log;
- secure copy does not enter Windows clipboard history/cloud sync.

### 3. Agent permissions and terminal control

1. Repeat a harmless write tool in read-only, ask, edit, auto, and yolo modes.
2. Ask the model to propose a clearly destructive command without executing it.
3. Start a long command, open a second AI conversation as an observer, wait/read
   from both, interrupt softly, and transfer control back to the user.
4. Open `hx` or `vim`, a pager, a REPL, and an alternate-screen monitor; pause
   the agent and resume after returning to the shell.

Expected:

- every mode follows its displayed target and grant lifetime;
- model-initiated work follows the selected mode and any higher-priority
  reusable rule; no hidden risk overlay silently changes the selected mode;
- only one conversation owns writes; observer cancellation does not cancel the
  command or another waiter;
- frame generation, dimensions, cursor, takeover state, and degraded capability
  remain visible and bounded.

### 4. Encrypted history

1. In installed mode, opt in, create a conversation, restart, restore, export,
   disable retention, and delete the ciphertext.
2. Repeat in portable mode while locked, after unlocking, after locking again,
   and after reopening. Exercise credential-storage migration with an existing
   envelope.

Expected:

- history is off by default and cannot be enabled while the portable vault is
  locked;
- restore contains user/assistant text only and revives no permissions, tool
  calls, budget, or write ownership;
- lock removes decrypted rows from memory; unlock restores them;
- export is explicit user-owned plaintext and delete removes the envelope before
  its durable key can be discarded.

### 5. Real SSH/SFTP and shell capability

1. Use the existing password and key-only test profiles without putting secrets
   in environment variables. Test PowerShell, bash, zsh, and fish where
   available, plus nested SSH, `sudo`, `tmux`, background output, disconnect,
   reconnect, and tab close.
2. From the AI tools, list and read a bounded SFTP file, inspect telemetry,
   forwarding, history, scripts, and notes, save one approved runbook, and queue
   one explicitly approved upload/download.

Expected:

- the host fingerprint is still explicit on first trust and credentials never
  reach logs or AI context;
- rich/basic/none capability is honest for each shell and never upgrades from
  unverified output;
- SFTP path, symlink, byte, encoding, generation, cancellation, and permission
  errors are typed; mutations use the visible transfer queue;
- closing the tab with SFTP or AI activity produces no crash or late callback.

### 6. MCP trust chain

Follow the four MCP checks in `V3_AI_ACCEPTANCE.md` using the absolute local
`ztermy_mcp_test_server.exe` path from the same build. Observe trust must not
expose tools; execute trust plus exact schema approval exposes only that digest;
   calls follow the active Read-only/Ask/Edit/Auto/YOLO mode and reusable rules,
   and schema drift or revoke removes the tool immediately. Ask/Edit expose the
   exact approval card; Auto/YOLO dispatch without an accidental per-call
   prompt unless an explicit ask rule matches.

### 7. MSI and portable interaction

1. Install the MSI per user, launch from Start, install the same version again,
   then uninstall. If an older private ztermy build is available, upgrade it
   first.
2. Launch the portable ZIP from a writable directory, create settings, move the
   whole directory, and launch again.

Expected:

- installation remains under LocalAppData and needs no elevation;
- same-version repair/upgrade succeeds, Start-menu identity is correct, and
  uninstall removes the application folder while preserving user data;
- portable settings and vault stay beside the executable and survive moving the
  directory.

## Environment-dependent release gates

Both duration commands below completed successfully, and their reports pass the
unified release-candidate evidence verifier:

```powershell
pwsh -NoProfile -File .\scripts\run_ai_concurrency_soak.ps1 `
  -BuildDirectory .\build\msvc-static-release `
  -DurationSeconds 7200 `
  -ReportPath .\build\msvc-static-release\ai-concurrency-soak-2h-schema2.json

pwsh -NoProfile -File .\scripts\run_terminal_stability_soak.ps1 `
  -BuildDirectory .\build\msvc-static-release `
  -DurationSeconds 28800 `
  -ReportPath .\build\msvc-static-release\terminal-stability-soak-8h.json
```

Also required before declaring the final release rather than an RC:

- the same critical matrix on the previous supported Windows 11 build when a
  VM or machine is available;
- five clean runs per supported evaluation task on the designated cloud model
  and one local/compatible model, meeting the thresholds in
  `V3_AI_ACCEPTANCE.md`.

## Approval rule

Tagging `v0.3.0` and marking the long-term V3 goal complete require the owner to
confirm the affected manual matrix or explicitly accept a documented exception.
An unchecked environment gate is reported as a release limitation; it is never
silently converted into a pass.
