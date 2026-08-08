# V2.4 SFTP navigation continuity

Status: implementation candidate for the `0.2.4` line

## Objective

V2.4 closes the largest remaining gap in the daily SFTP browsing workflow:
keeping remote-file navigation connected to the live terminal context. It
adds trustworthy terminal working-directory discovery, explicit locate/follow
actions, and a real lazily loaded tree view.

The release remains on the `0.2.x` line with codename `此`. It does not add
remote editing, telemetry, transfer pause/resume, or cosmetic placeholders for
unimplemented behavior.

## Required release work

- consume libghostty-vt's OSC 7, OSC 9, and OSC 1337 current-directory state;
- decode `file://` values, accept only normalized absolute POSIX paths, and
  never infer a path by injecting shell commands;
- expose explicit locate and opt-in follow actions in both regular and compact
  SFTP toolbars;
- provide list/tree switching with lazy independent directory requests,
  per-node loading/error state, keyboard expand/collapse, and visible focus;
- persist view mode and follow preference per saved profile in workspace
  schema v4 while migrating schema v1-v3 safely;
- retain the last useful listing when a tree node or direct navigation fails;
- complete English and Simplified Chinese strings, tests, runtime smokes, and
  static Release packaging for version `0.2.4`.

## Deliberate boundaries

- Current-directory tracking depends on a cooperating shell emitting a
  standard OSC sequence. The UI explains the unavailable state rather than
  polling or parsing prompts.
- Tree expansion shares the independent SFTP connection but not the
  latest-only current-directory request slot. Navigating the root invalidates
  stale node results by generation.
- Follow is opt-in and event driven. It issues no background polling.
- Explorer drag-out, remote telemetry, executable script triggers, filename
  encoding, configurable columns, and resumable transfers remain separate
  contracts.

## Completion evidence

V2.4 is complete only after domain/model/session/migration tests, the full
dynamic and static gates, real-window UI smokes, a read-only real-host SFTP
check, translation/format/static-analysis gates, and portable/MSI packaging
agree on `0.2.4`.
