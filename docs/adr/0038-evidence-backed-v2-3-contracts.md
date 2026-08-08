# ADR 0038: Evidence-backed V2.3 contracts

Status: accepted for V2.3

## Context

The post-V2 roadmap grouped compatibility, accessibility, performance, and
several productivity candidates into V2.3. Some items can be verified through
deterministic native tests. Others depend on physical Windows features or need
new backend contracts substantially larger than a patch release.

Treating every candidate as a UI checkbox would create misleading controls and
weaken the daily-use baseline. Treating Narrator, Contrast Themes, physical DPI,
or mixed-monitor behavior as unit-tested would create equally misleading
release evidence.

## Decision

V2.3 separates three kinds of work:

1. automated contracts for bounded history loading, large SFTP models,
   persistence migration, keyboard focus, lifecycle, and native runtime gates;
2. physical Windows acceptance for Narrator, Contrast Themes, mixed DPI,
   multi-monitor transitions, and long interactive sessions;
3. deferred capabilities that require a complete backend contract before any
   UI is exposed.

Remote path bookmarks and empty remote-file creation enter V2.3 because they
have small, persistent, testable contracts. Tree SFTP, drag-out download,
triggerable scripts, remote telemetry, and transfer pause/resume are deferred.

Performance gates use generous MSVC Debug ceilings to catch regressions and
record static Release observations separately. They avoid microbenchmark
claims and remain focused on user-visible bounded work.

## Consequences

- V2.3 can be called complete without fake parity for every NetCatty feature.
- Manual evidence remains a release requirement where automation cannot
  observe the actual assistive technology or physical display transition.
- Workspace state advances to schema v3 while preserving v1/v2 migration.
- Future tree, drag, script, telemetry, or resumable-transfer work must begin
  with its own domain/platform contract and tests, not toolbar placeholders.
