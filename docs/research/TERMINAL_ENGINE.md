# Terminal engine candidate assessment

Status: first integration gate passed

## Outcome

The first terminal-state implementation uses `libghostty-vt` through a
ztermy-owned C++23 interface and C ABI adapter. Contour remains the fallback
C++ engine candidate. Windows Terminal is used as a correctness and
Windows-integration reference rather than imported as a library.

The ConPTY transport, terminal state, and Qt Quick renderer remain separate
components. This lets the engine spike fail without replacing process I/O or
window code.

## Candidate comparison

| Candidate | Strengths | Main risks | Spike position |
| --- | --- | --- | --- |
| libghostty-vt | Modern VT coverage, grapheme-aware state, reflow, scrollback, C ABI, zero runtime dependencies | C API is not versioned yet; build introduces Zig | Selected for the first implementation |
| Contour vtbackend | Modern C++23, strong Unicode support, dirty state and mature terminal behavior | Large transitive CMake/vcpkg footprint; components are developed inside the full application | Fallback |
| Windows Terminal core | Excellent Windows behavior, VT parser, text buffer, MIT license | Repository is solution-oriented and tied to WIL/WinRT/DirectWrite infrastructure; no small supported CMake package | Reference |
| libvterm 0.3.x | Small MIT C library with callback API | Older VT scope; scrollback and product-level reflow remain host responsibilities; MSVC integration is not its primary distribution path | Baseline only |
| Independent engine | Full API control | Highest correctness and maintenance cost, already observed in the Rust prototype | Rejected as primary |

## Integration result

The initial build and ABI gate is complete:

1. Ghostty is pinned to revision
   `ae8727401d8c549671c36cdc326a94f47c94b635`, with the source archive hash
   checked by CMake.
2. `TerminalEngine` keeps Ghostty handles and headers out of application-facing
   code.
3. Zig 0.16.0 builds `ghostty-vt-static`; subsequent application-only
   incremental builds reuse the output.
4. MSVC dynamic Debug and static Release linkage both pass.
5. Tests cover invalid geometry, VT sequences split across writes, formatting,
   and resize reflow.

The current Windows build disables optional SIMD dependencies. This favors a
small, deterministic first integration over maximum parser throughput. The
choice must be benchmarked again once the live session and renderer exist.

The remaining gate is the renderer handoff. The GUI must consume an immutable
cell snapshot rather than formatted plain text or a live Ghostty terminal
handle. Unicode, IME, alternate-screen, selection, dirty-row, and sustained
output behavior remain part of that validation.

## Risks retained

- The upstream C API signatures are still in flux, so upgrades are deliberate,
  pinned changes.
- Zig is an additional build prerequisite and its cache path should be kept
  short on Windows.
- The adapter currently exposes plain text only for tests, diagnostics, and
  future clipboard operations. It is not the rendering API.
- Third-party license notices must be finalized before any public binary
  distribution.

## Evidence

- Ghostty describes `libghostty-vt` as a Windows-compatible, zero-dependency
  terminal-state library with a C ABI, while noting that API signatures remain
  in flux: <https://github.com/ghostty-org/ghostty>
- The pinned Ghostty build declares Zig 0.16.0 as its minimum version:
  <https://raw.githubusercontent.com/ghostty-org/ghostty/ae8727401d8c549671c36cdc326a94f47c94b635/build.zig.zon>
- The current C API is documented in the public umbrella header:
  <https://raw.githubusercontent.com/ghostty-org/ghostty/ae8727401d8c549671c36cdc326a94f47c94b635/include/ghostty/vt.h>
- Ghostling demonstrates the C API and a renderer-independent terminal:
  <https://github.com/ghostty-org/ghostling>
- Contour is an Apache-2.0 C++23 terminal with Windows support, but its Windows
  build uses a wider vcpkg and application dependency set:
  <https://github.com/contour-terminal/contour>
- Windows documents ConPTY as UTF-8 text interleaved with VT sequences and
  requires the host to own presentation and input serialization:
  <https://learn.microsoft.com/windows/console/createpseudoconsole>
