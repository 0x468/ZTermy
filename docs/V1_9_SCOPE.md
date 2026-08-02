# V1.9 scope: release-candidate hardening

V1.9 converts the accumulated V1.1–V1.8 feature set into a release-candidate
quality workspace. It does not add another large product surface.

## Visual reference audit

The 2026-08-02 Computer Use baseline compared the current NetCatty terminal
workspace with the current ztermy Debug Hosts workspace.

- Retain ztermy's clearer Hosts cards, quick-connect block, and master/detail
  host editor; the owner previously preferred these saved-host cards.
- Keep NetCatty's strengths as the terminal-workspace reference: one dense top
  command strip, terminal-attached SFTP/workbench panes, a compact transfer
  surface, an optional bottom composer, and icon-first secondary actions with
  themed tooltips.
- Do not copy NetCatty assets, branding, source, serial-console controls, or its
  exact styling. ztermy continues to use its own QML design system and C++
  implementation.
- Avoid a cosmetic radius reset. Consistency, state clarity, keyboard access,
  compact layout, and readable material layering take priority over matching a
  screenshot pixel for pixel.

## Delivery contract

1. Complete native accessibility integration: Windows high contrast, system
   colors, reduced motion, keyboard focus, accessible names, and opaque fallback
   surfaces.
2. Exercise Hosts, Settings, terminal, SFTP, scripts/history, composer, transfer
   center, menus, dialogs, tooltips, and empty/error states in both languages and
   themes at 100–200% DPI.
3. Stress lifetime and cancellation boundaries for window close, terminal tabs,
   session logs, SFTP sessions, transfers, and recovery journals.
4. Validate settings/profile/workspace/recovery migrations without automatic
   connections or command execution.
5. Produce dynamic Debug and static Release evidence, all code/QML/translation
   gates, runtime window gates, portable/MSI contracts, checksums, and a final
   release-candidate record.
6. Keep all unperformed owner, real-host, multi-monitor, high-contrast, and clean
   Sandbox checks in the deferred acceptance ledger with exact expectations.

V1.9 has no serial console, cloud sync, collaborative editing, AI assistant, or
standalone dual-pane file manager.

