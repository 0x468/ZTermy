# ADR 0002: Validate the terminal engine before product implementation

Status: proposed

## Context

Qt provides windowing, input, text, and graphics primitives but not a complete
terminal emulator. Terminal parsing, screen state, scrollback, Unicode, input
protocols, and efficient rendering form the largest technical risk.

## Proposed validation

Time-box a technical spike that:

1. Starts PowerShell through ConPTY.
2. Feeds output into a candidate terminal engine.
3. Renders a terminal grid through one custom Qt Quick item.
4. Supports input, resize, selection, CJK, IME, and alternate screen.
5. Measures input latency and sustained large-output frame time.

The first candidate is reuse of compatible modular components from Contour.
An independent minimal engine is the comparison case. No dependency is adopted
until its API, license, build footprint, Unicode behavior, and rendering
integration are documented.

## Decision criteria

- Correctness under common VT sequences
- Windows and MSVC build reliability
- C++23 and Qt integration cost
- Dirty-row and scrollback performance
- Unicode and IME behavior
- Maintainability and license compatibility

