# ADR 0022: Qt localization pipeline

Status: accepted for V1.3

## Context

ztermy currently ships an English interface, but the owner wants English and
Simplified Chinese first and a path that can add more locales without partial
or untracked translations. More text-heavy terminal history, shortcuts, and
workflow surfaces are planned, so localization should become a foundation
before those features multiply the source strings.

## Decision

V1.3 will migrate every user-facing QML and C++ string in one bounded pass to
Qt's translation system:

- English remains the canonical source language.
- QML uses `qsTr()` with stable component context; C++ QObject types use
  `tr()`, and non-QObject code uses `QCoreApplication::translate()` with an
  explicit stable context.
- Dynamic values use `%1` placeholders and plural-aware translations rather
  than translated-fragment concatenation.
- `QTranslator` loads the compiled Simplified Chinese `.qm` catalog when it is
  the effective language; English uses the canonical source strings and System
  resolves to one of those two languages. A language change calls
  `QQmlEngine::retranslate()` so normal UI text updates without restarting;
  native resources that cannot update safely may request a restart explicitly.
- Qt Linguist `.ts` catalogs are versioned; generated `.qm` files are build
  artifacts packaged through CMake.
- CMake runs `lupdate` and `lrelease`. CI fails when extraction changes the
  committed catalogs, a release locale contains unfinished entries, placeholder
  sets differ, or a new user-facing source string bypasses the translation
  wrappers.
- Terminal output, hostnames, usernames, paths, saved profile data, protocol
  tokens, and diagnostic logs are not translated. Accessibility names,
  validation, dialogs, empty states, installer UI, and system notifications are
  translated. Installer/bootstrapper UI owned by Windows Installer or another
  packaging technology remains outside this application catalog and can be
  localized with the future custom installer.

The language preference is global rather than per profile. Locale fallback is
requested locale -> base language -> canonical English; missing text must never
produce an empty control.

## Consequences

- V1.3 must include a complete existing-string migration rather than mixing
  translated and raw pages over several releases.
- Translation coverage becomes measurable and release-gated.
- Adding another locale requires a new catalog and completed coverage, not UI
  code changes.
- Source wording and translation keys need deliberate stability because casual
  English edits invalidate catalog entries.
