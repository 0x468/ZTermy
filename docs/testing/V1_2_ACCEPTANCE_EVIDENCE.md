# V1.2 acceptance evidence

Status: automated gates and owner-performed desktop acceptance passed.

Date: 2026-08-01

This record maps the V1.2 requirements to current implementation and test
evidence. It does not replace the interactive checklist in
[`V1_2_CREDENTIALS.md`](V1_2_CREDENTIALS.md).

## Requirement coverage

| Requirement | Evidence |
| --- | --- |
| Save before authentication | `saveHostProfileWithCredential` persists without a connection. `saveAndConnectPersistsBeforeConnectionOutcome` proves **Connect** saves the profile and credential before SSH starts and retains them after the connection fails. |
| Installed-mode secure storage | `WindowsCredentialVault` uses WinCred generic credentials. `installedControllerPersistsCredentialAcrossRestart` passed in an interactive desktop logon with a random, automatically removed test entry. |
| Portable encrypted storage | Portable-vault tests cover minimum password strength, restart locking, wrong-password rejection, master-password change, tamper rejection, absence of plaintext, and a fresh encrypted envelope when the same secret is rewritten. |
| Global selection and migration | Coordinator and AppController tests cover System, Portable, and Session selection; destination read-back verification; target rollback after a partial write; persisted selection; optional source retention; and leaving an uninitialized empty Portable store. |
| Cleanup and residual management | Tests cover per-profile forget/delete, active and inactive store clearing, retained WinCred copies, stale Session references after restart, and rollback after a partial batch deletion. |
| Opaque profile metadata | Profile-store schema tests verify that profiles contain only a credential reference and never password or passphrase data. |
| Host workflow | AppController and real-window keyboard smoke tests cover hostname-derived names, full selection retained after an actual pointer press/release, saved-credential readback, masked reveal control, copy-without-credential, the in-layout master-detail editor, actual click-away dismissal, credential dialogs, confirmations, default Hosts startup, and keyboard focus order. |
| Portable startup UX | Controller tests cover locked/unlocked state and the real-window QML gates cover the persistent title-bar action and updated hit-test metrics. Owner acceptance verifies the startup unlock prompt and its dismissed/reopened flow. |
| Secret handling | `SensitiveByteArray` is move-only and clearing is tested. Source/log scans found no credential, master-password, private-key-content, or terminal-input logging. |

## Automated gates

The following gates passed against the current worktree:

- MSVC Dynamic Debug build and CTest: 22/22.
- MSVC Static Release build and CTest: 22/22.
- C++ formatting and clang-tidy with warnings as errors: 52 translation units.
- QML formatting, qmllint, compact/regular layout smoke, and keyboard smoke.
- Windows Credential Manager AppController integration: 3 passed, 0 failed,
  0 skipped in the interactive desktop logon.
- Portable and AppController credential suites: 10 consecutive passes during
  the fault-injection audit.
- WiX per-user MSI contract and portable archive structure validation.
- `git diff --check`.

WiX reports the already accepted ICE61, ICE69, and ICE91 warnings for the
same-version per-user package. The repository's installer contract checker
passes and confirms LocalAppData installation, one executable, product icon,
Start-menu shortcut, same-version upgrade, and uninstall directory removal.

## Release artifacts

```text
1af37e0c0d49826b8590bbb610ba8c9f27cff6fb135ac1913ff3bd54c1be8ea5  ztermy-0.1.0-windows-x64-portable.zip
475f30fd45ff536b18edf7e9e6ba3d1a0400a77a396b5ddec184f811ae4116ac  ztermy-0.1.0-windows-x64.msi
```

The portable archive contains only its top-level directory,
`portable.flag`, and the statically linked `ztermy.exe`.

## Owner-performed desktop acceptance

The owner completed the checklist in `V1_2_CREDENTIALS.md` on 2026-08-01,
including password and authorized private-key connections to the real SSH
fixture, Windows Credential Manager lifecycle and restart behavior, Portable
vault restart and migration behavior, and dark/light normal/narrow UI review.
The final focused rerun also passed the generated-name selection behavior, the
in-layout Hosts/detail reflow, and click-away dismissal.
