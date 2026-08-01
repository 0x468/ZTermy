# ztermy V1.3 scope

Status: accepted by the owner on 2026-08-02; environment-blocked MSI ICE
revalidation is deferred

## Goal

V1.3 establishes complete, release-gated localization and global font
personalization before ztermy adds more text-heavy workflows. English remains
the canonical product language and Simplified Chinese becomes the first
complete translation. Settings follow NetCatty's information architecture at
the product level while retaining ztermy's native Qt implementation.

## Included

1. Add one global language preference with **System**, **English**, and
   **简体中文** choices.
2. Resolve **System** to Simplified Chinese for a Chinese Windows UI language
   and to English for every other currently unsupported locale.
3. Persist the preference in `settings.json` while retaining backward
   compatibility with every existing settings schema.
4. Load Qt `.qm` catalogs before the first QML component is created and update
   the existing window at runtime without restarting.
5. Migrate every ztermy-owned, user-facing QML and C++ string to Qt translation
   APIs. Dynamic text uses placeholders or plural-aware translations rather
   than translated fragments.
6. Translate accessibility names, validation, dialogs, empty/recovery states,
   settings, host workflows, and application-owned terminal status text.
7. Keep terminal output, hostnames, usernames, paths, profile data, protocol
   tokens, logs, and secret material outside translation.
8. Version the Linguist `.ts` catalog, compile `.qm` artifacts during the
   normal build, and embed the release translation in installed and portable
   packages.
9. Add gates for catalog freshness, unfinished translations, placeholder
   parity, and user-facing source strings that bypass translation wrappers.
10. Reorganize Settings into **Application**, **Appearance**, **Terminal**, and
    **Security**. Application is an About-like product page; language and UI
    font live under Appearance, matching the NetCatty reference hierarchy.
11. Use the Windows/Qt system UI font by default and provide a searchable,
    installed-font picker for an optional global UI-font override. Preserve
    Windows' script-aware fallback chain for mixed English and Chinese text.
12. Replace terminal font text entry with a searchable installed-font picker.
    Show monospaced fonts by default, allow an explicit **Show all installed
    fonts** opt-in, and warn when a proportional font is selected for the fixed
    terminal grid.
13. Detect supported OpenType programming-ligature features and expose a global
    ligature control. The renderer shapes only compatible single-width runs,
    breaks runs at IME/style/selection/cursor boundaries, and never changes the
    terminal cell model.

## UX and accessibility

- Language changes take effect immediately and preserve the current page,
  open terminal sessions, focus route, and unsaved settings draft.
- Controls may grow for Chinese text but must not clip, overlap, or introduce
  horizontal scrolling at the existing compact and regular breakpoints.
- Mouse, keyboard, screen-reader names, light/dark themes, reduced motion, and
  100–200% DPI behavior remain equivalent in both languages.
- Missing translations fall back to canonical English and never yield empty
  controls.
- Font pickers support search, pointer use, and keyboard navigation. Font
  settings are global and do not add redundant per-host appearance profiles.

## Deferred

- Additional locales beyond English and Simplified Chinese.
- Per-profile UI or terminal appearance overrides.
- Translation of remote terminal output, shell content, saved user data, logs,
  or third-party/system-provided UI.
- Command-history and reusable-command sidebars, shortcut customization, SFTP,
  AI, sync, and other new text-heavy workflows. These follow the localization
  foundation in a later product milestone.
- NetCatty serial functionality remains outside ztermy's product boundary.

## Acceptance

V1.3 is complete only when translation quality gates, Dynamic Debug and Static
Release suites, the real-window runtime matrix, packaging inspection, and the
owner-performed bilingual desktop checklist all pass. Evidence is recorded in
`testing/V1_3_LOCALIZATION.md`.
