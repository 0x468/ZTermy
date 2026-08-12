# V3 0.3.0 release-candidate acceptance

Status: automated candidate complete; owner and environment-dependent checks
pending

Date: 2026-08-12

Identity:

- version: `0.3.0`;
- codename: **糸**;
- verse: **「剪不断，理还乱，是离愁」**;
- supported release platform: Windows 11 x64;
- candidate package source: `05eb0a4`; the final application reliability change
  is `7339b20`.

## Environment

- Microsoft Windows 11 Pro 10.0.26200, x64;
- Qt 6.8.3 static Release and official Qt 6.8.3 dynamic Debug;
- Microsoft Visual Studio 2022 17.14.15, MSVC x64;
- CMake 4.4.0 and Ninja 1.13.1;
- PowerShell 7.6.4;
- clang-format/clang-tidy 22.1.6.

## Automated evidence

The following gates completed with exit code zero:

1. Dynamic Debug configure/build and 102/102 CTest tests. The final-source run
   took 102.22 seconds.
2. Static Release configure/build and 102/102 CTest tests. The initial full run
   took 143.89 seconds; the final-source run took 89.94 seconds. Both included
   the 32-cycle MCP process lifecycle stress.
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
   - static Release developer run: four iterations, 158.877 seconds, zero
     failures;
   - static Release formal duration run: 179 iterations, 7207.7 seconds, zero
     failures. The content-free report spans 2026-08-12 07:36:34Z through
     09:36:42Z and belongs to the static Release build directory.
8. WiX generation, ICE validation, decompilation, and structural inspection.
   Only the retained and reviewed ICE61, ICE69, and ICE91 warnings were emitted.
9. Final portable archive extraction, `portable.flag` inspection, and packaged
   executable lifecycle smoke.
10. The pinned key-only LAN test host completed 100 authenticated
    connect/disconnect cycles in one `SshTerminalSession` with no failure. A
    second focused real-host run passed explicit host-key confirmation, bounded
    shell-history and telemetry reads, authentication rejection classification,
    remote close, and the 16 ms input-queue P95 gate. The test used only the
    owner-approved host identity, public fingerprint, username, and private-key
    path; no password, passphrase, key content, or terminal command entered the
    command line or report.
11. The formal AI-idle local-terminal stability gate completed 28800.148
    seconds with zero failures. The content-free report spans 2026-08-12
    15:45:37+08:00 through 23:45:38+08:00, records the static Release build
    directory, and passes the unified release-candidate evidence verifier.
12. The final-source protected clipboard contract completed 100 consecutive
    Release repetitions after adding bounded Win32 clipboard contention retry.
    The same contract passed in the final Debug and Release 102-test suites.

The formal soak reports are generated at
`build/msvc-static-release/ai-concurrency-soak-2h.json` and
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
  `5d9de3006ccb0a602fc713eb880c0119ae61d8c5a670f5f2f52f11b71fe64aa2`;
- `ztermy-0.3.0-windows-x64.msi` — SHA-256
  `57640e8a48b55fc85a2f2ff6adeb13ac520cecf7ebff256b11c0f3475a8166de`;
- `SHA256SUMS.txt`;
- `release-manifest.json`.

The manifest and checksum file are authoritative if packaging is run again,
because MSI and ZIP container metadata can change artifact hashes without a
source change.

This final bundle was rebuilt and verified on 2026-08-13. WiX ICE validation
completed through the Windows Installer service with only the reviewed ICE61,
ICE69, and ICE91 warnings; structural decompilation, portable extraction, the
packaged executable lifecycle smoke, and the unified RC verifier all passed.

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

Both duration commands below completed successfully, and their reports pass the
unified release-candidate evidence verifier:

```powershell
pwsh -NoProfile -File .\scripts\run_ai_concurrency_soak.ps1 `
  -BuildDirectory .\build\msvc-static-release `
  -DurationSeconds 7200 `
  -ReportPath .\build\msvc-static-release\ai-concurrency-soak-2h.json

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
