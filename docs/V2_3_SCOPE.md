# V2.3 compatibility, accessibility, and deferred productivity

Status: implementation and automated release candidate for the `0.2.3` line;
physical accessibility/multi-monitor acceptance remains open

## Objective

V2.3 turns the accepted V2.2 product shell into an evidence-backed daily-use
candidate. It concentrates on Windows compatibility, accessibility, measured
performance, `0.2.x` migration safety, and two SFTP productivity gaps that can
be completed without inventing unsupported backend behavior.

The release does not expand ztermy into a remote-development suite. NetCatty
remains the workflow and density reference; ztermy retains its native Qt/C++
architecture, security boundaries, brand, and explicit exclusions.

## Required release work

### Compatibility and accessibility

- retain full keyboard operation and visible focus for Hosts, Settings,
  terminal actions, SFTP, dialogs, and transfer controls;
- verify Windows Contrast Themes and Narrator with the physical OS features;
- verify 100%-200% scaling, physical DPI changes, and mixed-DPI monitor moves;
- retain native resize, work-area maximize, Snap Layouts, reduced motion, and
  clean long-running shutdown behavior;
- visually review English and Simplified Chinese at regular and compact sizes.

Automated tests can prove object roles, names, focus routes, theme contracts,
synthetic DPI geometry, and lifecycle invariants. They cannot honestly claim
that Narrator spoke the intended text or that a physical mixed-DPI transition
looked correct. Those checks remain explicit manual release evidence.

### Measured performance

- local terminal startup must reach a running session within the existing
  five-second runtime gate;
- sustained output, search, and compact-to-regular resize must keep the GUI
  heartbeat below the established 250 ms maximum-gap gate;
- reading an 8 MiB PowerShell history file is bounded to a 1 MiB tail and must
  return 2,000 recent entries within 2 seconds in an MSVC Debug build;
- sorting 10,000 SFTP entries must complete within 5 seconds and filtering the
  resulting model within 1 second in an MSVC Debug build;
- transfer progress updates must preserve cancellation/action stability and
  pass the existing sustained lifecycle tests.

The Debug budgets are regression ceilings, not performance targets. Static
Release measurements are retained in the V2.3 acceptance record so future
work can compare like-for-like hardware and build modes.

### SFTP productivity

- remote path bookmarks are per saved profile, ordered newest-first, capped at
  32 entries, persisted in workspace schema v3, and available from the compact
  path toolbar;
- an empty remote file can be created through a keyboard-accessible New file
  action in both wide and compact toolbar layouts;
- workspace schema v1 and v2 data migrate to v3 with an empty bookmark list;
- malformed, duplicate, unsafe, or oversized bookmark state is rejected.

## Evaluated but deferred

- Tree SFTP requires a hierarchical, lazily loaded model with independent
  cancellation and error state; the current flat directory model is not
  repackaged as a cosmetic tree.
- Explorer drag-out download requires a Windows data-object and delayed-render
  contract that keeps the remote transfer alive after the drag begins.
- Script triggers require a real execution, variable, trust, and failure model;
  command snippets remain honestly labelled.
- Remote telemetry requires shell/OS adapters and an explicit privacy/polling
  contract.
- Transfer pause/resume requires protocol-level resumability, offset integrity,
  and recovery metadata; no inert pause controls are exposed.

These decisions are recorded in ADR 0038. They are candidates for a later
`0.2.x` release or the V3 decision gate, not implicit V2.3 promises.

## Completion evidence

V2.3 is complete only when:

1. the new persistence, SFTP command, performance, translation, format,
   clang-tidy, and non-network test gates pass in dynamic Debug and static
   Release as applicable;
2. the retained layout, keyboard, terminal-render, resize, DPI, appearance,
   shutdown, and real-host read-only SFTP smokes pass;
3. the manual matrix in `testing/V2_3_ACCEPTANCE.md` is executed or explicitly
   marked pending without being misrepresented as automated evidence;
4. portable and installed packaging produce versioned `0.2.3` artifacts;
5. every source, catalog, package, and release document agrees on version
   `0.2.3` and codename `此`.
