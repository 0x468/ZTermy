# V2.7 acceptance: terminal productivity controls

## Automated evidence

- MSVC dynamic Debug: 44/44 CTest cases passed (38.95 s final pass).
- MSVC static Release: 44/44 CTest cases passed (48.14 s).
- C++ formatting and the complete Debug `clang-tidy` target passed with warnings
  treated as errors.
- All 41 QML files passed formatting and `qmllint` without warnings.
- The Simplified Chinese catalog contains 819 finished source messages and
  passed the translation-catalog gate.
- Real-host UTF-8/GB18030 switching passed in Debug and static Release against
  the dedicated trusted key-authentication fixture. No credential material or
  terminal input is recorded in this document or in application logs.
- Eight static Release runtime gates passed: native window, appearance,
  resize, 100/125/150/200% DPI, UI layout, keyboard, terminal rendering, and
  lifecycle.
- Release bundle assembled at
  `build/msvc-static-release/package/release/ztermy-0.2.7-windows-x64`:
  - portable ZIP: 18,855,604 bytes,
    SHA-256 `0bdaf9718809539df24aa2bf998820f24d0b46945c7833ec5b2b22efb447f9ee`;
  - MSI: 15,257,600 bytes,
    SHA-256 `32c312c7fab0413e9d2c6e6f96b80e2fedb2b3298be0fefe469ae753296ae1c5`.

The MSI was generated successfully. WiX ICE01-ICE105 contract validation could
not run in this Windows environment: WiX exited with `WIX0217`/217 because the
Windows Installer Service was unavailable to the ICE host. This is an
environmental verification limitation, not a substituted pass; the bundle was
assembled directly from the generated non-empty artifacts and its hashes were
verified by `AssembleReleaseBundle.cmake`.

## Manual acceptance

1. Connect a saved SSH profile. Open **Host keyword highlighting**, add an ASCII
   rule and a Chinese wide-character rule with different colors, toggle case
   matching, close/reopen ztermy, and confirm the rules persist and selection
   still overrides highlight colors.
2. Open a quick (unsaved) SSH connection and add a rule. Confirm it works for the
   current tab but is not added to any saved profile after the tab closes.
3. Open **Session terminal settings**, change font size, cursor, opacity, and
   default colors. Confirm only the active tab changes; close it and verify a new
   tab uses global defaults.
4. Against a trusted GB18030-capable host, select GB18030 and print/type Chinese
   text, including text whose bytes arrive across multiple reads. Switch back to
   UTF-8 and confirm both input and output are correct without reconnecting.
5. Start script recording. Run commands from composer, history, and snippets;
   pause and run another command; resume and stop. The review contains only the
   non-paused structured commands. Raw typing and password prompts never appear.
6. Replay the draft and confirm command order/delays. Close the tab during replay;
   playback must cancel without dispatching into another tab or crashing.
7. Resize from wide to the minimum supported width. Hidden toolbar actions must
   appear in **More**, remain keyboard reachable, and never overlap identity,
   telemetry, or terminal content. Check themed tooltips in light and dark modes.
