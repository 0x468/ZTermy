# V3 0.3.0 release-candidate acceptance

Status: automated candidate complete; owner and environment-dependent checks
pending

Date: 2026-08-12

Identity:

- version: `0.3.0`;
- codename: **糸**;
- verse: **「剪不断，理还乱，是离愁」**;
- supported release platform: Windows 11 x64;
- candidate application source: `2d6a4c6` plus test-only acceptance hardening
  `45cc411` and `1b8b70f`.

## Environment

- Microsoft Windows 11 Pro 10.0.26200, x64;
- Qt 6.8.3 static Release and official Qt 6.8.3 dynamic Debug;
- Microsoft Visual Studio 2022 17.14.15, MSVC x64;
- CMake 4.4.0 and Ninja 1.13.1;
- PowerShell 7.6.4;
- clang-format/clang-tidy 22.1.6.

## Automated evidence

The following gates completed with exit code zero:

1. Dynamic Debug configure/build and 102/102 CTest tests.
2. Static Release configure/build and 102/102 CTest tests. The initial full run
   took 143.89 seconds; the final post-hardening warm run took 90.80 seconds.
   Both included the 32-cycle MCP process lifecycle stress.
3. C++ format, QML format, QML lint, translation, branding, executable metadata,
   and package contracts.
4. clang-tidy with warnings as errors over all 219 C++ translation units in the
   static compilation database.
5. The inherited 30-second local-terminal interaction gate and its 16-test
   persistence/lifecycle core.
6. Eight serial real-window gates: Windows work area and native caption,
   appearance, resize/hit testing, DPI 100/125/150/200, responsive AI layout and
   accessibility, keyboard navigation, terminal rendering, and lifecycle
   shutdown.
7. Provider-independent AI concurrency soak:
   - dynamic Debug: three iterations, 120.296 seconds, zero failures;
   - static Release: four iterations, 158.877 seconds, zero failures.
8. WiX generation, ICE validation, decompilation, and structural inspection.
   Only the retained and reviewed ICE61, ICE69, and ICE91 warnings were emitted.
9. Final portable archive extraction, `portable.flag` inspection, and packaged
   executable lifecycle smoke.

The static soak report is generated at
`build/msvc-static-release/ai-concurrency-soak.json`. It contains test names,
timings, iteration counts, and failures only; it contains no terminal or AI
content.

## Release handoff

Authoritative directory:

```text
build/msvc-static-release/package/release/ztermy-0.3.0-windows-x64
```

It contains exactly:

- `ztermy-0.3.0-windows-x64-portable.zip` — SHA-256
  `36bb9fc706925c75d3791be98a6d836c15c8307fc78731af1e72dd7b5ebcb1a0`;
- `ztermy-0.3.0-windows-x64.msi` — SHA-256
  `f39220dfced9ed97086281417c01ba16ffa000a5cf42f92cda3c9fe23873c6a2`;
- `SHA256SUMS.txt`;
- `release-manifest.json`.

The manifest and checksum file are authoritative if packaging is run again,
because MSI and ZIP container metadata can change artifact hashes without a
source change.

## Owner acceptance — exact checks

Use a disposable provider key and test host. Do not paste either into an issue,
diagnostic export, or this document.

### 1. Provider settings and request privacy

1. Open Settings > AI and add OpenAI Responses, Ollama, and one compatible
   endpoint as available. Save a cloud key in the selected credential vault.
2. Close and reopen Settings and restart ztermy.
3. Exercise an invalid key, offline endpoint, TLS failure, 429 response, and a
   valid request.
4. Open the AI privacy diagnostics and export ordinary application diagnostics.

Expected:

- keys remain masked and survive only in the selected vault;
- errors distinguish authentication, configuration, transient, and offline
  causes without exposing request content or credentials;
- diagnostics show coarse endpoint scope, policy, and counts only;
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

1. Repeat a harmless write tool in observer, ask-each-write, ask-first-write,
   session-auto, and saved-host-auto modes.
2. Ask the model to propose a clearly destructive command without executing it.
3. Start a long command, open a second AI conversation as an observer, wait/read
   from both, interrupt softly, and transfer control back to the user.
4. Open `hx` or `vim`, a pager, a REPL, and an alternate-screen monitor; pause
   the agent and resume after returning to the shell.

Expected:

- every mode follows its displayed target and grant lifetime;
- model-initiated high-risk work asks even in automatic mode unless the owner
  explicitly grants the exact session/target override;
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
every call still asks, and schema drift or revoke removes it immediately.

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

These deliberately remain unclaimed until their reports exist:

```powershell
pwsh -NoProfile -File .\scripts\run_ai_concurrency_soak.ps1 `
  -BuildDirectory .\build\msvc-static-release `
  -DurationSeconds 7200 `
  -ReportPath .\build\msvc-static-release\ai-concurrency-soak-2h.json

$env:ZTERMY_RUN_LOCAL_SOAK_GATE = "1"
$env:ZTERMY_LOCAL_SOAK_SECONDS = "28800"
.\build\msvc-static-release\ztermy_local_terminal_session_tests.exe `
  survivesSustainedInteractionWithoutLatencyGrowth
Remove-Item Env:ZTERMY_RUN_LOCAL_SOAK_GATE
Remove-Item Env:ZTERMY_LOCAL_SOAK_SECONDS
```

Also required before declaring the final release rather than an RC:

- 100 real SSH connect/disconnect/tab-close cycles on the current supported
  Windows 11 release;
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
