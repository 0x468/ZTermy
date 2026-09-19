# ADR 0124: Unified application theme and inline selection

Status: accepted by owner through theme fitting room V2; implemented with focused automated acceptance; manual acceptance and non-release gates remain below.

## Decision

- One theme supplies terminal colors and derives the application surface/ink ladder. The independent light/dark UI selector is removed. A theme's `dark` attribute determines interface appearance, including native window styling.
- Two modes: follow system uses separate `lightTheme` and `darkTheme` IDs; fixed uses `terminalTheme` regardless of Windows appearance. The existing `theme` field retains `system` for follow-system and `dark`/`light` for fixed (the selected theme determines which); these values remain readable for previous data.
- Settings schema 36 adds the two system slots. Schema 35 upgrades preserve all existing fields, including the fixed terminal theme; system slots initially use ztermy Light/Dark. Missing custom themes fall back to built-ins; system slots enforce the expected brightness at resolution and save.
- Windows appearance changes are delivered from NativeWindow via Main to AppController. The controller alone resolves the active theme and sends palette changes through existing session queues. During explicit draft preview, system changes are remembered but do not override the preview; ending it uses the latest system state.
- Settings contain an inline keyboard-accessible card grid and a local full-window preview. Hover/focus changes only the local preview. Selecting a card previews the whole application, including detached windows. Apply saves; leaving settings or discarding restores persisted settings. No second theme selection dialog remains.
- The surface ladder uses the approved fitting-room formulas in `Theme.surfacePalette`, shared by the live UI and thumbnails. Native materials remain separate; high contrast keeps its system-color overrides. No terminal cells are modeled as QML objects; previews are static illustrations.
- Purple is the default ztermy accent. Following a theme's accent is an explicit separate choice, alongside Windows/custom accents.
- Imports and removals remain immediate library operations, separate from draft Apply. Built-in themes cannot be deleted.

## Verification contract

Focused coverage: schema-35 migration/36 round trip, invalid slot IDs, system changes, fixed mode, preview cancellation and save failure. Runtime acceptance must cover both system slots, hover isolation, keyboard navigation, Apply/Discard, and detached windows. Web evidence alone is not native acceptance.

The fitting room remains a design reference, not a release artifact. No merge is implied by this ADR.

## Native acceptance route

Run the built executable with `--theme-settings-smoke --data-dir <fresh isolated directory>`.
This opens the actual Settings QML, activates cards with the keyboard, checks local
versus whole-window preview, Discard, Apply and both system slots, and saves light,
dark and compact screenshots. The simulated system changes exercise the controller
entry point; changing the real Windows preference and detached-window appearance
remain manual acceptance items.

During this iteration, the four focused CTests (application-settings,
terminal-theme-catalog, app-controller, translation-catalog) passed, as did focused
clang-tidy for the theme controller and settings store/tokens. QML quality checking
reports the pre-existing `TerminalSplitNode.startCopyMode` warning.
Dynamic Release builds successfully, and `--theme-settings-smoke` passes on the
final binary with light/dark/compact screenshots in `build/theme-final-20260919/`.

Remaining non-release gates: the wider `--ui-keyboard-smoke` stops at the terminal
settings focus route (`settingsTerminalWordDelimiters` expected,
`settingsTerminalGrid` observed). The code-size ratchet rejects the controller
header's six added API/state lines and controller implementation's one added
comparison line; its baseline has not been increased. These checks are not being
reported as passed, and no full regression or package acceptance is claimed.
