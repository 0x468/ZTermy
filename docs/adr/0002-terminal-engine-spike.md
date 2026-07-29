# ADR 0002: Validate the terminal engine before product implementation

Status: accepted for spike

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

The first candidate is `libghostty-vt` through its C ABI. Contour's
`vtbackend` remains the C++ comparison case, and Windows Terminal's core is a
behavioral reference. An independent terminal engine is no longer a primary
candidate because the previous Rust prototype demonstrated the maintenance
cost of owning parser, screen, scrollback, Unicode, and input behavior
together.

No dependency is adopted until its API stability, license, build footprint,
Windows/MSVC integration, Unicode behavior, and renderer handoff have been
measured. The ConPTY transport is implemented behind a terminal-session
boundary so this choice does not block local process integration.

## Decision criteria

- Correctness under common VT sequences
- Windows and MSVC build reliability
- C++23 and Qt integration cost
- Dirty-row and scrollback performance
- Unicode and IME behavior
- Maintainability and license compatibility
