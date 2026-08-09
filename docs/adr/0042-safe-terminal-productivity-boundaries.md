# ADR 0042: Bound terminal productivity features at domain and transport edges

## Status

Accepted for 0.2.7.

## Context

Keyword highlighting, per-session appearance, terminal encoding, and command
recording look like presentation features but cross rendering, persistence,
transport, and security boundaries. Naively matching arbitrary regular
expressions during paint, converting text in QML, or recording the raw terminal
input stream would create latency, packet-boundary corruption, and credential
exposure risks.

## Decision

1. Host highlighting persists as validated literal rules on SSH profiles. A
   Qt-free matcher produces a bounded visible-cell style map once per render;
   selection remains visually dominant.
2. Session appearance lives on the terminal-tab state and is never written to a
   host profile. Global settings remain the sole persistent appearance layer.
3. Encoding is a transport concern. The SSH worker converts UTF-8 application
   input to the selected remote encoding and incrementally decodes remote bytes
   back to UTF-8 before logging and VT parsing. UTF-8 and Windows code page 54936
   (GB18030) are the initial supported codecs.
4. Script recording observes only explicit `runTerminalCommand` actions from
   application-owned command surfaces. Raw input, paste, and authentication
   prompts bypass the recorder. Steps and delays are bounded and replay is tied
   to the originating tab generation.
5. QML owns popovers, focus, tooltips, and responsive overflow; C++ owns all
   validation, persistence, conversion, recording state, and dispatch.

## Consequences

- Paint and memory costs have explicit upper bounds and wide characters retain
  correct cell coverage.
- GB18030 sequences split across SSH packets decode correctly; invalid input is
  rejected without exposing its content.
- Recorded drafts are useful for professional workflows but cannot silently
  capture passwords typed into a shell prompt.
- Per-session customization is lightweight and predictable, at the cost of
  being discarded when the tab closes.
- A future regex engine, persistent script runtime, or additional codec must
  receive its own resource, trust, and migration contract.

## Rejected alternatives

- **Regex matching directly in QML or paint:** unpredictable worst-case cost.
- **One codec conversion per received packet:** corrupts split multibyte input.
- **Record all bytes sent to the PTY:** captures secrets and terminal control
  traffic without reliable semantic boundaries.
- **Persist appearance on every SSH profile:** duplicates global policy without
  sufficient value for this personal client.
