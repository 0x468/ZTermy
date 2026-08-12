# Pre-V3 program: final Windows 11 daily-use baseline

Status: completed through `0.2.14`; owner selected AI integration for V3

## Objective

Complete V2.9 through V2.14 on the `0.2.x` line, leaving ztermy as a highly
available, complete, responsive, and maintainable personal Windows 11 SSH
workspace. The owner subsequently selected native AI integration for `0.3.x`.
The approved product program is recorded in `V3_AI_PROGRAM.md`, with architecture,
security, research, and acceptance documents linked from that program.

All `0.2.z` releases retain the codename `此` and the approved verse.
The selected V3 direction uses codename `糸` and verse
`「剪不断，理还乱，是离愁」` across the `0.3.z` line.

## Execution order

1. V2.9: SSH connection depth and resilience.
2. V2.10: native SSH port forwarding.
3. V2.11: terminal workspace continuity.
4. V2.12: recursive and batch SFTP.
5. V2.13: scripts and local notes.
6. V2.14: cross-feature stabilization and final pre-V3 release.

Each milestone receives its own scope, ADRs for significant decisions, automated
evidence, retained manual acceptance, static Release artifacts, and a
Conventional Commit. A later milestone may refine an earlier implementation but
may not silently weaken its accepted behavior.

## Non-negotiable invariants

- Terminal, SSH, SFTP, forwarding, scripts, persistence, and diagnostics never
  block the GUI or Qt Quick render thread.
- Passwords, passphrases, private keys, agent messages, terminal input, script
  variable secrets, and unredacted secret-bearing commands are never logged.
- Every background worker is bounded, cancellable, owned, and joined during
  shutdown. Detached work is prohibited.
- External input, persisted data, remote output, filenames, and imported scripts
  have explicit size, encoding, schema, and failure contracts.
- Runtime state and credential state remain separate. Installed builds use the
  Windows credential boundary; portable builds use the encrypted portable vault.
- One terminal viewport remains one custom Qt Quick item; no feature may turn
  terminal cells into QML object trees.
- NetCatty is a workflow and density reference only. ztermy owns its C++/QML,
  assets, text, persistence, and Windows-native behavior.
- Unsupported features have no inert controls. Deliberate V2 exclusions remain
  Serial, Telnet, Mosh, EternalTerminal, cloud sync, collaboration, AI, and
  remote editing. V3 explicitly changes only the AI boundary; the other
  exclusions remain in force.

## Milestone release gate

Every version must pass, in both applicable Debug and static Release builds:

- compilation, C++ formatting, full clang-tidy, QML formatting/qmllint, and the
  complete translation gate;
- all unit, persistence-migration, integration, and non-real-host tests;
- the eight serial real-window gates, including the 100%–200% DPI matrix;
- focused real-host checks for every affected network path and orderly shutdown;
- portable ZIP, per-user MSI contract inspection, checksums, and artifact smoke;
- visual review of all new primary, empty, loading, failure, recovery, hover,
  focus, keyboard, narrow, light, and dark states.

High availability in this personal desktop context means deterministic recovery,
bounded retry/backoff, explicit degraded states, no silent data loss, and clean
teardown. It does not claim clustered service availability.

## Final V2.14 acceptance

V2.14 closes only when the whole supported product—not merely the newest
feature—passes a documented long-duration matrix. The matrix includes repeated
connect/disconnect, direct and multi-hop SSH, concurrent terminals, forwarding,
large terminal histories, recursive transfers, interrupted recovery, scripts,
notes, damaged persistence, credential lock/unlock, theme/language changes,
mixed DPI where physically available, installer upgrade/uninstall, and crash
diagnostic handoff.

Physical or environment-dependent checks that cannot be automated remain listed
with exact procedures and expected results. They may be accepted or explicitly
waived by the owner, but are never reported as executed without evidence.
