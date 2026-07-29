# ADR 0010: Versioned application settings

Status: accepted

## Context

V1 exposes appearance and terminal behavior preferences that affect QML,
the custom Win32 window, and the terminal renderer. These preferences must
survive restart in installed, portable, and custom-data modes without using a
second platform-specific settings location. Invalid or partially written
configuration must not place the window or terminal into an unsafe state.

## Decision

Application settings use the `settings.json` path resolved by
`ApplicationPaths`. The file has an explicit schema version and is read and
written through `ApplicationSettingsStore`, independently from QML and Win32
types.

The V1 schema contains:

- system, dark, or light theme preference;
- window opacity and Windows backdrop preference;
- terminal font family and size;
- terminal-controlled or user-selected cursor style and blinking;
- copy-on-select and multiline-paste confirmation behavior.

Input is strictly typed and range checked. Window opacity is limited to
`0.5`–`1.0`, terminal font size to `8`–`32` pixels, and the font-family name
must be non-empty and bounded. Unknown enum tokens, missing fields, fractional
font sizes, oversized files, malformed JSON, and unsupported schema versions
are rejected.

Missing files use safe defaults. Writes use `QSaveFile` so a failed update
does not replace the last complete document. The application controller owns
the current validated value and emits one change notification only after a
successful write. UI preview state remains separate until the user applies it.

## Consequences

- QML, the renderer, and Win32 integration consume one validated settings
  source rather than persisting their own values.
- Portable mode keeps preferences inside the portable data root.
- A malformed settings file falls back to defaults and produces a generic
  diagnostic without logging user-entered values.
- Schema changes require an explicit migration or version decision.
- Light theme, backdrop, and renderer controls can be implemented in stages
  without changing the persistence contract.
