# ADR 0025: Centralized actions and shortcuts

Status: accepted

Date: 2026-08-02

## Context

ztermy's initial QML shell used several fixed `Shortcut` objects and direct
button handlers. V1.4 added more terminal workbench operations, making that
approach vulnerable to duplicated labels, inconsistent availability, shortcut
conflicts, and settings that cannot reliably describe what the UI executes.

## Decision

Introduce an application-layer action registry. Each action has a stable id,
category, translation source text, default portable key sequence, palette
visibility, repeat policy, and terminal-context requirement.

The registry validates and normalizes shortcut overrides. `AppController`
persists overrides with the versioned application settings and exposes a
QML-safe action list plus invokable record/reset/trigger operations. Triggering
an action authorizes its id and context in C++ before emitting one request to
the QML shell. QML then performs the small set of presentation-level routing
operations or calls an existing native controller operation.

Bindings are single-step `QKeySequence::PortableText` values. An empty value is
an intentional unbind. Exact duplicate effective sequences are rejected.
Unmodified printable input is rejected so customization cannot accidentally
steal normal terminal typing.

## Consequences

- Buttons, command palette rows, settings, and shortcuts share one definition.
- New features must register an action before exposing it through several UI
  surfaces.
- View navigation remains explicit QML glue instead of moving presentation
  state into C++.
- The schema gains a map of overrides keyed by stable action id; labels and
  translated strings do not enter persistence.
- Multi-stroke chords, OS-global hotkeys, argument-bearing commands, and
  user-authored actions require later extensions rather than hidden special
  cases in V1.5.
