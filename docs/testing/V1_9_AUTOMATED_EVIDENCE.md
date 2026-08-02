# V1.9 automated release-candidate evidence

Status: automated gates passed; owner and real-host acceptance deferred

Date: 2026-08-02

## Candidate scope

This record covers the V1.9 release-candidate hardening changes before their
Conventional Commit is created. It is reproducible evidence for the working
tree, not an immutable release sign-off. The exact owner-run scenarios remain
in [DEFERRED_MANUAL_ACCEPTANCE.md](DEFERRED_MANUAL_ACCEPTANCE.md).

No real SSH host was contacted during this pass.

## Automated results

- Dynamic Debug and static Release builds completed with MSVC, CMake, and
  Ninja.
- Dynamic Debug: 37 of 37 non-real-host CTest tests passed.
- Static Release: 37 of 37 non-real-host CTest tests passed.
- Both configurations passed the seven real-window gates: work-area sizing,
  appearance, resize, 100/125/150/200% DPI, responsive layout, keyboard
  operation, and terminal rendering.
- All 87 C++ translation units passed clang-tidy with diagnostics treated as
  errors.
- C++ formatting, all 33 QML formatting checks, qmllint, and the translation
  gate passed.
- The Simplified Chinese catalog contains 710 finished translations and no
  unfinished entry.
- Session-log restart stress completed 25 start/write/flush/stop cycles.
- Transfer-manager destruction cancelled a deliberately held worker and
  released its client.
- The ordinary QML smoke path entered a real Qt event loop, closed its window,
  shut down the controller, and released scene-graph resources.
- The static package contract produced a per-user MSI, portable ZIP, artifact
  manifest, and SHA-256 list. The MSI validator reported only the three
  reviewed ICE61, ICE69, and ICE91 warnings documented in ADR 0011.

## Generated artifact snapshot

These artifacts are build outputs and are intentionally not committed:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `ztermy-0.1.0-windows-x64-portable.zip` | 18,602,739 | `4449745399d79b45c9e8c00d2634a117a3dfdeeb5ec9a7e87fe8cc5a0d0aef8e` |
| `ztermy-0.1.0-windows-x64.msi` | 15,351,808 | `f88d1d14235341e2a3d6d4a4b15ae86a6b1b39678b49af04856c14d6535993c0` |

The release directory contained exactly those two artifacts plus
`SHA256SUMS.txt` and `release-manifest.json`; both manifests contained the same
hashes.

## Remaining evidence

The following are not represented as automated passes:

- live switching among actual Windows contrast themes and Narrator review;
- the complete English/Chinese route on physical displays at each DPI;
- repeated shutdown while real SSH/SFTP resources are active;
- an installed upgrade/uninstall and portable replacement using this exact
  candidate;
- clean Windows Sandbox launch and install;
- authorized password/private-key real-host SSH and SFTP transfer checks.

Those items are explicitly pending rather than failed. They must be performed
against a later immutable V2 candidate before that artifact is called an
accepted release.
