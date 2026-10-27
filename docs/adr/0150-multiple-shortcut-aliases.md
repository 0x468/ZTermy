# ADR 0150: Multiple shortcut aliases with Windows-compatible paste

- Status: Accepted
- Date: 2026-10-28

## Decision

An application action may have zero to eight single-chord key combinations.
Each is independently editable/removable in the existing shortcut settings.
Multi-step chords remain unsupported. Normalize and deduplicate combinations;
validate every alias against all other actions before committing any change.
The first alias remains the compact menu/palette display shortcut.

Application settings schema 47 stores shortcutOverrides values as JSON arrays.
Schemas through 46 migrate a nonempty string to a one-element list and an empty
string to an empty list. Explicit custom/unbound actions are not expanded to new
defaults. Preserve unrelated preferences and opaque credential references.
When loading, existing custom aliases take precedence over defaults, including
new Ctrl+V and Shift+Insert defaults; remove only conflicting aliases. Unknown
future schemas remain read-only failures.

Terminal paste defaults to Ctrl+Shift+V, Ctrl+V and Shift+Insert. The focused
terminal leaf owns all three, in main and detached windows. They share clipboard
reading, multiline confirmation and backend bracketed-paste encoding. Remove the
hard-wired Shift+Insert path so unbinding really returns the chord to the terminal.
Do not special-case clipboard managers or inspect foreground remote processes.
Removing Ctrl+V restores its original Shell/Vim meaning. Text-input controls
retain their normal editing shortcuts and the global summon hotkey remains a
single OS registration, outside these per-action aliases.

## Verification

Use registry/config/controller tests for normalization, conflicts, atomic failure,
single-value migration, explicit unbinding and persistence. Native keyboard tests
cover alias add/edit/remove in settings and all three paste bindings in main and
detached terminals, plus raw Ctrl+V passthrough after unbinding. Only isolated
test shells are used; pasted fixture text is never executed.
