# 0.5.3 acceptance

Date: 2026-10-03. Local Windows 11 / MSVC 2022 / Qt 6.8.3.

## Feature boundaries

- `461d220`: off-by-default SSH reconnect text history; settings schema 44
  reads schema 43 without changing unrelated preferences. History is bounded
  to the most recent 4 MiB of text, stays in memory and is imported into a
  fresh protocol engine. Explicit pane close releases it. See ADR 0137.
- `1f54e1f`: single-row, clipped/elided AI tool summaries; full parameters and
  results remain selectable/scrollable and expansion survives tool updates.
  See `AI_LONG_CONTENT.md` for reproduction, native captures and execution
  evidence limits. A timeout is not claimed as confirmed command success.
- The local TODO records both as completed in 0.5.3. The Hermes/TUI exit
  report remains explicitly deferred by the owner, not silently resolved.

## Release preflight cleanup

The full structure gate exposed source-size regressions accumulated since the
last milestone. Split pane/workbench/title-input/reconnect runtime helpers,
clipboard helpers, native preference handling, terminal font-style constants,
AI tool headers and the diagnostics settings card into bounded owners. Keep
the existing budgets/baseline unchanged. The extracted QML preserves object
names and explicit original translation contexts.

An incremental static link also exposed a test object referring to the old
two-argument SSH start symbol. Rebuild the static tree from clean generated
artifacts; do not change the history API to accommodate a stale object or
exclude that test. Verification below must correspond to final source.

The clean full-analysis run then exposed an incomplete hand-maintained
`ztermy_test_binaries` list: the AI evaluation target's module response file
had not been generated. Collect all declared `ztermy_*_tests` targets instead,
so new test modules cannot silently fall out of build prerequisites. Rebuild
both configurations and rerun analysis after fixing this gate.
The same preflight also builds the two performance-tool targets, whose module
response files were likewise absent after cleaning. Keep all production,
test and tool translation units in the full analysis instead of excluding
the affected files.

## Final verification

- Debug: 133/133 CTest passed from the complete test-target build (547.37 s).
  Static Release: 133/133 passed (473.06 s). Each runs with parallelism 12;
  native capture matrices are serial, and configurations do not overlap
  those matrices. Neither run produced a new Shell DLL-init popup event.
- Full static Release clang-tidy: all 325 production/test/tool translation
  units passed, warnings treated as errors. Format, structure/dependency,
  100-QML quality, 2394 finished translations and native asset gates passed.
- ZIP: fresh extraction contains exactly `ztermy.exe` and `portable.flag`;
  the executable has product/file version 0.5.3 and matches the tested EXE.
  Bundle manifest and SHA-256 files identify both ZIP and MSI.
- MSI: final bundled/source hashes match. Explicit ICE (skip disabled) and
  structural/payload inspection passed. ICE61 (intentional same-version
  upgrade), ICE69 (CPack shortcut references a same-feature file component),
  and ICE91 (intentional per-user location) warn; no ICE errors. The existing
  build cache's skip option is not used as evidence of ICE success.
- Final native long-content captures were visually reviewed in light/dark
  palettes: clipped summary, independently scrollable details and full
  answer body no longer overlap. Retention includes main/detached windows.
- The final extracted components additionally pass the full native AI
  capture fixture at 150% scale (20.12 s). Both ZIP/MSI-extracted executable
  payloads match the tested EXE's SHA-256 and version 0.5.3, start the real
  QML/native window successfully and exit cleanly with isolated data paths.
  No new Shell DLL-init event occurs during these payload launches.

Final logs (local/ignored): `build/milestone-debug-tests-0.5.3-complete.log`,
`build/milestone-static-0.5.3-complete.log`,
`build/milestone-static-tests-0.5.3-final.log`, and
`build/milestone-msi-0.5.3-validation.log`, and
`build/milestone-ai-150-0.5.3.log`. Extracted payload startup logs are in
`build/msvc-static-release/test-data/package-0.5.3-smoke-{1,2}/`.

## Scope limitations

Native SSH retention includes a controlled authenticated loopback fixture and
failed/retried connection tests, not a real external host network-fault matrix.
AI execution tests use a loopback provider and fake terminal; they prove full
input and no repeated side effect on answer retry, not arbitrary real-provider
or remote-Shell correctness. Runtime screenshots use synthetic non-secret data.

Do not infer a new Sandbox installation test from MSI extraction or payload
launch. No tag, push or GitHub Release is part of this version update.

## Owner-requested settings polish and GitHub release

After the initial milestone, the owner requested removing explanatory small
print and keeping startup-restore children directly below their parent. The
window settings now have no muted explanatory paragraphs. Reconnect text
history is an independent root-level switch after the three restore children;
its default and persistence are unchanged. The concise-settings rule is also
recorded in repository guidance.

The UI layout fixture now asserts visual order, shared parent, root/child
padding, and dependent enabled states with startup restore on and off. The
independent history switch remains enabled in both cases. Dynamic and static
Release rebuilt successfully. QML quality (100 files), translations (2388),
format checks and focused static Release clang-tidy for main.cpp passed.
The native layout fixture completed successfully, with application-settings
captures reviewed at regular and compact widths. The native event gate found
no new Shell DLL-init errors, and the code structure gate also passed.
The initial launcher stopped waiting at 55 seconds, without killing the
process; its subsequent completion is recorded by the fixture's success log.
Evidence: `build/settings-layout-0.5.3-final/`.

Per the owner's instruction, this follow-up did not repeat the full CTest,
full clang-tidy, MSI/ZIP, or Sandbox installation matrices. The GUI Setup was
rebuilt from the refreshed dynamic EXE, existing deployed dependencies and
0.5.3 signed Explorer identity; no host trust or installation was changed.
The owner explicitly requested committing, tagging 0.5.3 and uploading the
new Setup and static EXE to GitHub. No pre-existing 0.5.3 tag or release was
found; older release tags and unrelated preview drafts remain untouched.
