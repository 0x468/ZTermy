# V2: stable personal native SSH workspace

V2 consolidates the completed V1.1–V1.9 milestones into one supported Windows
11-first product contract. It is an integration and release-readiness milestone,
not another broad feature expansion.

## Included contract

- local ConPTY and SSH terminals with Unicode, CJK, IME, selection, clipboard,
  alternate-screen, resize, search, and scrollback behavior;
- saved hosts, host-key trust, password/private-key authentication, Windows
  Credential Manager, and the encrypted portable vault;
- terminal-attached SFTP with background transfers, conflict decisions,
  cancellation, retry, recovery, and a global transfer center;
- centralized actions, command palette, editable shortcuts, shell history,
  scripts/code snippets, composer, session logging, diagnostics, and safe
  workspace restoration;
- the custom Windows 11 window shell, Snap Layout integration, materials,
  independent window/terminal appearance, accent sources, reduced motion,
  high contrast, English/Chinese localization, and DPI-aware layout;
- dynamic developer builds, static portable and per-user MSI distribution,
  versioned migrations, checksums, and repeatable non-real-host release gates.

## Explicitly outside V2

Serial consoles, cloud sync, collaboration, AI assistants, remote editing, and
a standalone dual-pane file manager are not V2 requirements. NetCatty and other
products remain interaction references only; their code, assets, branding, and
serial-specific UI are not part of ztermy.

## Completion rule

Implementation reaches a **V2 candidate** when all automated gates pass for
version `0.2.0` and the remaining physical, real-host, accessibility, and
installer checks are recorded precisely. It becomes an **accepted V2 release**
only after the owner performs or explicitly waives those checks against the
immutable candidate hashes. Pending evidence is not a software failure, but it
must never be reported as tested.
