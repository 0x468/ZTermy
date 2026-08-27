---
name: ztermy-change-checks
description: Apply ztermy-specific compatibility and verification checks when changing persisted schemas, interface or branding assets, translations, QML controls, versioning, packaging, or release behavior. Do not use for read-only inspection.
---

# ztermy change checks

Use this checklist after locating affected symbols with CodeGraph. Select only
the sections that match the change; do not run the full release matrix for
every small edit.

## Preserve durable contracts

### Persisted data

- Treat every stored schema as an independent, durable contract. Never
  decrement, reuse, or renumber a version already written by any build.
- `ApplicationSettings` is currently schema 28. Verify the current constant in
  `src/core/config/ApplicationSettings.cpp` before changing it; source wins if
  this note has not yet been updated.
- A new persisted application setting uses the next schema, adds a fixture for
  the immediately preceding real document, preserves unrelated fields, and
  verifies rewrite to the current schema.
- Removed features leave readable tombstone versions. Unknown future schemas
  remain read-only failures. Keep secrets outside `settings.json`.
- Read `docs/adr/0101-monotonic-application-settings-schema.md`. Check the
  owning store for profile, workspace, script, note, transfer, or AI data
  instead of using the application version.
- `SshProfileStore` is currently schema 7. Preserve schema-6 jump-host data
  while defaulting the authentication and terminal-startup timeouts introduced
  by schema 7.

### Interface and brand assets

- Production interface SVGs use `viewBox="0 0 20 20"`, `currentColor`, and no
  hard-coded hex colors. Add them to the CMake resource list.
- `cmake/VerifyInterfaceIcons.cmake` currently guards 59 SVGs. Update its exact
  count when adding or removing an icon and add contract-critical names to its
  required list.
- Brand output keeps a nine-layer ICO plus PNG sizes 16, 20, 24, 32, 40, 48,
  64, 128, and 256. `cmake/VerifyBrandingAssets.cmake` is authoritative.
- Run the `interface-icon-assets` or `branding-assets` CTest that matches the
  change.

### QML, text, and architecture

- Keep external I/O off the GUI and Qt Quick render threads. Keep one custom
  terminal viewport item rather than per-cell QML objects.
- Reuse design-system controls and `AppToolTip`; preserve keyboard-visible
  focus even when suppressing mouse-only focus outlines.
- Update `translations/ztermy_zh_CN.ts` for user-visible source text and run the
  translation gate. Source locations are metadata, not translation identity.
- Record a new ADR only for a durable technical or product contract, not an
  ordinary implementation detail.

## Choose proportionate verification

### Iteration loop

Build affected targets, run focused CTest names, and run applicable format,
QML, translation, or asset gates. Example:

```powershell
cmake --build --preset msvc-dynamic-debug --target <affected-target> ztermy_format_check
ctest --test-dir build\msvc-dynamic-debug -R '^<focused-test>$' --output-on-failure
```

For QML changes also run `ztermy_qml_quality_check` and the smallest relevant
runtime smoke. An asset-only change does not require unrelated SSH or AI tests.

### Feature commit

Run owning-module tests, adjacent integration tests, relevant QML smoke, and a
focused manual check for observable UI or native behavior. Build static Release
incrementally when the owner will test that binary.

### Milestone, RC, tag, or package

- Run formatting, QML quality, translation, and asset gates.
- Run full clang-tidy.
- Run Debug and static Release CTest with
  `--parallel 12 --output-on-failure`.
- Run relevant window, IME, DPI, material, terminal-latency, portable, and
  installer acceptance checks. Package only after the tested build succeeds.

Lower parallelism if GUI smoke or memory pressure becomes unstable. Do not hide
a reproducible race by lowering concurrency without diagnosing it.

## Finish the change

- Inspect `git diff --check` and preserve unrelated user changes.
- State which checks ran and which manual checks remain.
- Use a Conventional Commit message. Do not claim UI or native behavior
  complete from unit tests alone.
