# Roadmap to V2

Status: completed; V2 `0.2.0` accepted from binary source `8f8fccf` on
2026-08-03

## Purpose

This roadmap records the completed path from the Windows 11-first native
ztermy V1.6 file-transfer milestone to the V2 implementation candidate. It is
an execution record rather than a promise to copy every feature of a reference
product. NetCatty, Windows Terminal, Termius, Tabby, and other clients remain
behavioral references only.

The final record is
[`docs/testing/V2_RC_8F8FCCF.md`](testing/V2_RC_8F8FCCF.md). Retained items in
`docs/testing/DEFERRED_MANUAL_ACCEPTANCE.md` remain an explicit regression
matrix and must not be silently described as tested.

## Invariants through V2

- C++23, Qt Quick, MSVC, CMake, and Ninja remain the primary stack.
- QML owns presentation; C++ owns state, I/O, security, persistence, and
  platform integration.
- Terminal cells remain one custom-rendered viewport, never a QML object tree.
- SSH terminal, SFTP browsing, and background transfers use independent,
  non-blocking sessions.
- Passwords, passphrases, private keys, terminal input, and secret-bearing
  commands are never logged.
- Windows-specific behavior remains behind platform boundaries where practical.
- Serial features, copied reference-product assets, and copied third-party
  source remain outside the product boundary.

## V1.6 — SFTP and background transfer foundation

Deliver a terminal-attached remote file browser and an application-wide bounded
transfer system:

- independent SFTP session lifecycle per active SSH terminal;
- remote path navigation, refresh, filter, hidden-file toggle, selection, create,
  rename, and delete;
- streaming upload/download with cancellation, progress, temporary upload names,
  atomic replacement, and explicit conflict decisions;
- bounded FIFO transfer scheduling with retry and stable error codes;
- Action Registry, localization, accessibility, tests, documentation, and
  package gates;
- global transfer queue surface that outlives the originating terminal panel.

Deferred from V1.6: standalone dual-pane manager, remote editor/auto-sync,
permission editor, compressed transfer, and cross-host copy.

## V1.7 — Session productivity

- promote remote-file operations into a polished terminal workflow;
- add download/upload entry points, drag/drop contracts, recent remote paths,
  and transfer notifications;
- finish command history/script ergonomics and shell-aware capture boundaries;
- provide session metadata and useful host telemetry without blocking the
  terminal;
- persist safe session layout state without restoring secrets or silently
  reconnecting;
- expand actions and user-customizable shortcuts for every stable command.

## V1.8 — Search, logging, scripts, and observability

- session log start/stop/export with explicit redaction boundaries;
- scalable terminal/global history search and remote-file filtering;
- script library organization, metadata, import/export, and predictable run or
  insert behavior;
- actionable diagnostics for SSH/SFTP/transfer failures without sensitive data;
- latency, queue, memory, and repaint budgets enforced by synthetic stress tests;
- recovery behavior for interrupted transfers and abnormal application exit.

AI integration is not implied by this milestone. It requires a separate privacy,
provider, cost, and secret-handling decision.

## V1.9 — Release-candidate hardening

- deep UI/UX consistency pass across Hosts, terminal, settings, workbench, SFTP,
  dialogs, menus, tooltips, empty states, and transfer surfaces;
- complete English and Simplified Chinese catalogs with translation gates;
- keyboard-only operation, focus order, screen-reader names, reduced motion,
  high contrast, light/dark themes, and 100–200% DPI checks;
- performance and lifetime stress tests for resize, IME, tabs, SSH, SFTP, and
  cancellation/shutdown;
- installed and portable upgrade/migration contracts, artifact identity,
  checksums, and clean uninstall expectations;
- no known critical crashes, heap corruption, secret leakage, or data-loss bugs.

## V2 — Stable personal native SSH workspace

V2 is reached when the following product contract is satisfied:

- dependable local and SSH terminals with correct Unicode, IME, selection,
  clipboard, resize, alternate-screen, and scrollback behavior;
- saved hosts, host-key trust, system and portable credential vaults, and clear
  locked/unavailable states;
- production-ready terminal-attached SFTP and application-wide transfers;
- centralized actions, command palette, customizable shortcuts, scripts,
  history, composer, search, and session logging;
- coherent native Windows 11 windowing, materials, themes, accent behavior,
  accessibility, localization, diagnostics, and packaging;
- documented architecture and migrations, repeatable automated release gates,
  and completed owner acceptance evidence.

The standalone dual-pane file manager, remote editing, cloud sync, collaborative
features, serial console, and AI assistant are not required for V2 unless the
owner explicitly promotes them into scope.

## Stage gate

Each milestone closes only when:

1. focused behavior tests pass;
2. the full configured test suite passes without real-host opt-in tests;
3. formatting, clang-tidy, QML formatting, QML lint, and translation gates pass;
4. dynamic debug and static release builds succeed;
5. package contracts succeed where applicable;
6. any unperformed runtime validation is recorded in the deferred acceptance
   ledger with exact steps and expected outcomes;
7. changes are committed with Conventional Commits.
