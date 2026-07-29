# Terminal engine candidate assessment

Status: spike decision, not a dependency commitment

## Outcome

The first terminal-state spike will target `libghostty-vt` through its C ABI.
Contour remains the fallback C++ engine candidate. Windows Terminal is used as
a correctness and Windows-integration reference rather than imported as a
library.

The ConPTY transport, terminal state, and Qt Quick renderer remain separate
components. This lets the engine spike fail without replacing process I/O or
window code.

## Candidate comparison

| Candidate | Strengths | Main risks | Spike position |
| --- | --- | --- | --- |
| libghostty-vt | Modern VT coverage, grapheme-aware state, reflow, scrollback, C ABI, zero runtime dependencies | C API is not versioned yet; build currently introduces Zig; Windows embedding needs local verification | First |
| Contour vtbackend | Modern C++23, strong Unicode support, dirty state and mature terminal behavior | Large transitive CMake/vcpkg footprint; components are developed inside the full application | Fallback |
| Windows Terminal core | Excellent Windows behavior, VT parser, text buffer, MIT license | Repository is solution-oriented and tied to WIL/WinRT/DirectWrite infrastructure; no small supported CMake package | Reference |
| libvterm 0.3.x | Small MIT C library with callback API | Older VT scope; scrollback and product-level reflow remain host responsibilities; MSVC integration is not its primary distribution path | Baseline only |
| Independent engine | Full API control | Highest correctness and maintenance cost, already observed in the Rust prototype | Rejected as primary |

## Toolchain gate

`libghostty-vt` is not added to the shared build yet. The development machine
does not currently expose `zig` on `PATH`, and the upstream C ABI is explicitly
described as still changing. The spike must therefore:

1. Pin an exact upstream revision.
2. Isolate the C ABI behind a ztermy-owned adapter.
3. Keep Zig out of normal incremental application builds where practical.
4. Prove MSVC Debug and static Release linkage.
5. Render only an immutable terminal snapshot on the Qt render path.

## Evidence

- Ghostty describes `libghostty-vt` as a Windows-compatible, zero-dependency
  terminal-state library with a C ABI, while noting that API signatures remain
  in flux: <https://github.com/ghostty-org/ghostty>
- Ghostling demonstrates the C API and a renderer-independent terminal:
  <https://github.com/ghostty-org/ghostling>
- Contour is an Apache-2.0 C++23 terminal with Windows support, but its Windows
  build uses a wider vcpkg and application dependency set:
  <https://github.com/contour-terminal/contour>
- Windows documents ConPTY as UTF-8 text interleaved with VT sequences and
  requires the host to own presentation and input serialization:
  <https://learn.microsoft.com/windows/console/createpseudoconsole>
