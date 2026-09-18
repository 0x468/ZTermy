# ADR 0122: settings navigation with a search index and per-row reset

Status: accepted for UI V2

## Context

The settings pane listed eight categories as a flat rail and the only way to
undo an edit was "Reset to defaults", which discarded every draft at once.
Rows were plain labels, so nothing told the user which values differed from
the defaults, and the defaults themselves only lived in
`config::ApplicationSettings` member initialisers, invisible to QML.

## Decision

- `SettingsCategoryRail` groups the categories by intent (General,
  Connections, Assistant, About) and owns the search index. Each index entry
  is `[row key, category, title, keywords]`; an empty key opens the category
  only. Matching is a substring test over title, keywords and category title,
  capped at eight results. Enter or activating a result emits
  `rowRequested(category, key)`; the rail clears its own field.
- `SettingsPane.jumpToRow` selects the category, sets `highlightedRow` to
  the key, restarts a 2.4 s timer and, once the page has laid out, scrolls
  the highlighted row into view with a 96 px lead.
- `SettingsRowLabel` is the row caption for every grid row that has a
  default. It takes `dirty` and `highlighted` bindings and emits `reset`; the
  pane restores only that draft. Row keys equal the settings keys used by
  the defaults map, so search, highlight and reset share one vocabulary.
- `AppController::applicationSettingsDefaults` returns the default value of
  every user-facing application setting as a `QVariantMap`, built from a
  default-constructed `config::ApplicationSettings` and the same token
  functions the persisted settings use. QML binds `dirty` against this map
  and never repeats a default literal.
- Category buttons are static declarations with ids so keyboard smokes can
  reach them when the rail scrolls; the rail scrolls as a whole in short
  windows.

## Consequences

- Adding a setting means adding a defaults-map entry, a search-index entry
  and a `SettingsRowLabel`; the keyboard smoke asserts the search -> jump ->
  per-row reset path so regressions surface in `--ui-keyboard-smoke`.
- Settings that have no persisted default (shortcuts, AI providers,
  credential storage) keep plain labels and appear in search as
  category-only entries.
- The defaults map is a snapshot of `ApplicationSettings` defaults; a new
  default value only needs the member initialiser changed.
