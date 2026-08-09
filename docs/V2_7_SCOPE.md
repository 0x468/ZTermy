# V2.7 scope: terminal productivity controls

Status: implemented for `0.2.7`

V2.7 closes the supported terminal-toolbar gaps identified in the frozen
NetCatty comparison without importing its source, assets, or branding. The
release adds four connected capabilities: host keyword highlighting, temporary
session appearance, real SSH encoding conversion, and safe structured command
recording.

## Included

- Saved SSH profiles persist at most 16 literal keyword rules. Each rule has a
  stable id, pattern, optional foreground/background color, enabled state, and
  case policy. The first matching rule wins.
- Rendering scans only the current visible terminal snapshot, limits patterns
  to 128 characters and matches to 512 per frame, and preserves selection as
  the highest-priority visual state. Wide-character cells share one match.
- Temporary session settings override font family/size, ligatures, terminal
  background opacity, cursor shape, and default foreground/background colors.
  Closing the tab discards them; profiles do not acquire appearance settings.
- SSH sessions support UTF-8 and GB18030. Input/paste conversion occurs before
  transport writes; output uses a streaming decoder that retains incomplete
  GB18030 sequences between packets. Local ConPTY sessions remain UTF-8/native.
- Structured recording captures commands executed through the composer,
  history, or command snippets. It supports record, pause/resume, stop/review,
  JSON clipboard export, clear, and bounded timed replay.
- The terminal toolbar uses owned icons, themed tooltips, keyboard activation,
  and width-based overflow fallbacks so actions remain reachable without
  overlapping the viewport.

## Security and performance bounds

- Password prompts, raw keyboard input, paste payloads, and arbitrary PTY input
  are never recorded. Recorded commands are not written to logs.
- The recorder stores at most 512 steps; commands are limited to 64 KiB and
  recorded delays to 60 seconds. Paused time is excluded.
- Highlight rules are literal rather than regular expressions. This avoids
  catastrophic expression behavior and keeps paint cost bounded.
- Encoding errors produce a localized status and do not log the rejected input.
- Replay is generation-cancelled when a tab closes or a newer playback starts.

## Deferred

- Persistent script libraries with variables, triggers, conditions, shell
  adapters, or unattended execution;
- regular-expression keyword rules and profile-level appearance settings;
- terminal video/frame recording, macros based on raw keystrokes, and password
  prompt capture;
- additional legacy encodings beyond UTF-8 and GB18030;
- split panes, port forwarding, resumable transfers, and Explorer drag-out.

## Acceptance boundary

Automated tests cover persistence migration, codec packet boundaries, wide-cell
highlighting, recorder state/bounds, controller integration, translations, and
QML loading. Physical runtime acceptance must still exercise the toolbar at
wide/narrow sizes, a trusted GB18030 SSH target, light/dark themes, and tab-close
cancellation as listed in `testing/V2_7_ACCEPTANCE.md`.
