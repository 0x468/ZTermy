# AI sidebar long-content regression

## Reproduction and correction

A tool summary containing newlines painted an approximately 810-pixel text item
inside a 34-pixel header, across subsequent tool cards and the assistant answer.
The answer's own Markdown height was correct. The fix renders summaries as
single-line plain-text previews, elides them to available width and clips the
header. Arguments/results retain their full stored content and scroll/select
independently. Expanding a tool survives updates to its presentation array while
its owning message delegate remains alive; this is bounded transient UI state,
not persisted conversation data.

## Automated evidence

`ai-long-content-runtime` uses the real QML and conversation model with synthetic
presentation data. It makes no provider or Shell requests. Six tool cards cover
running-to-complete transitions, long multiline command arguments, long output
rows, bounded detail viewports, full-text selection, scrolling and stream/final
answer geometry. Three sidebar widths (280, 440, 620) run under actual light and
dark palettes, with palette assertions and top/answer screenshots. Geometry
checks wait for QML layout instead of assuming a fixed render delay.

The controller provider-loopback test covers both a short command and a long
multiline command. A fake terminal verifies exact CR-normalized command input,
one dispatch only, full argument preservation, explicit timeout state and
response retry without repeating a side effect. This is not evidence of remote
Shell execution or a real provider understanding every long command. Existing
frame-idle results remain `idle_unverified`, not confirmed command success.

Focused owning/adjacent gates: conversation model/store, command echo/tracker,
frame tracker/tool, wait tool, turn runner, action dispatcher, controller,
translations, QML native window and the new long-content runtime. Static Release
also runs the native capture matrix at 150% scale. Milestone results belong in
the version acceptance record, not inferred from these focused checks.

2026-10-03: Debug and static Release each passed all 13 focused gates. The
static capture matrix passed at 150% scale, with no new Shell DLL-init events.
Format/QML/translation checks and affected static Release clang-tidy passed.
The initial synthetic controller could not shadow a read-only inherited model
property; the fixture was corrected before reproduction. An overlapping
cross-configuration run exhausted the initial 60-second budget and checked
layout too early. The final six-case matrix waits for geometry (bounded to 3
seconds per answer transition), has a 120-second workload budget and passes
without dropping any checks. Runtime captures are under each preset's
`test-data/ai-long-content-runtime/` directory.
