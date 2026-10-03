# Hidden title-trigger surface acceptance (2026-10-03)

Version: 0.5.2. Status: focused runtime verification and owner acceptance passed.
The owner confirmed native acceptance on 2026-10-03 and authorized this commit.
This iteration is not a release or installer rebuild.

## Change

The nearly transparent reserved strip exposed the native backdrop above opaque
Settings/Hosts pages. Hidden chrome now extends the current content surface.
Hosts also extend their navigation panel at its live width. Terminal and detached
windows share the workspace tint, with a terminal-colored 1/255 input floor only
when the workspace is fully transparent. Persistent chrome and the opaque revealed
overlay are unchanged. The twelve-pixel strip, resize boundary, hover delays,
slide animation, pointer-held Tab switching and stationary input shield are not
redesigned.

## Capture route

Run `scripts/run_isolated_shell_regression.ps1 -Preset msvc-dynamic-debug
-TestRegex '^title-trigger-material-runtime$'` after the isolated preset build.
The runtime rejects launches without `ZTERMY_TEST_ISOLATED_SHELLS=1`, a canonical
`<executable-directory>/test-data/title-trigger-material-runtime` data directory,
or an empty initial session list. It creates and closes only its two clean CMD
fixtures, restores settings/pointer/topmost state, and does not clear existing
sessions. Native activation is limited to the owned main/detached fixture windows.

The matrix covers Light/Dark; solid, transparent, Glass (`aero`), Acrylic, Mica
and Mica Alt; 0%, 45%, 100% requested opacity; Settings, Hosts and Terminal in the
main window plus a detached terminal; and both window activation phases.
Mica/Mica Alt retain their fixed workspace alpha, rather than claiming adjustable
opacity. Assertions check captured strip pixels, expected workspace alpha,
navigation width/color and absence of another tint layer. Scene/desktop images
are saved together; desktop appearance is reviewed separately from scene alpha.

Debug matrix passed in 354.99 seconds: 288 probes and 576 images under
`build/msvc-dynamic-debug/test-data/title-trigger-material-runtime/captures/`.
Representative Light/Dark Settings and Hosts desktop images were visually reviewed.
Debug focused CTest passed 6/6, including the capture matrix, immersive title-bar
interaction (46.62 seconds), native QML startup, window hit testing, window state
and theme catalog. Static Release passed four adjacent unit checks and two native
UI checks (6/6 total); its interaction smoke completed in 34.20 seconds. The large
capture matrix was run in Debug, not repeated in static Release. Both isolated
runtime gates reported no new Shell DLL-initialization popup events.
The original interaction smoke remains separate with its unchanged 60-second
budget; this much larger capture matrix permits 480 seconds for paired readbacks,
including inactive-window frame waits. No interaction timeout was increased.

The first capture run timed out at 120 seconds and revealed a test lifecycle bug:
leaving Settings correctly cancelled preview, so later images used saved defaults.
Its evidence is retained in `test-data/title-trigger-material-first-timeout-20261003`.
The final test reapplies the fixture after page navigation and asserts the actual
material alpha. The failed run is not counted as acceptance.

Both Debug and static Release compile with C++ format and 98-file QML checks.
Focused static-Release clang-tidy for `src/main.cpp`, including the new header,
passes after two type-deduction fixes. No user-visible strings, schema, version,
certificate or installer configuration changed. Full CTest/package/long-running
remote-session and mixed-DPI acceptance are not claimed for this iteration.

Static acceptance executable: `build/msvc-static-release/ztermy.exe`, SHA-256
`2a368c996eab0373e7131e2f23c664e7801354ede5c42261ddebfe3f9481194d`.
