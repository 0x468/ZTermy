# Repository guidance

## Product boundary

- ztermy is a Windows 11-first native SSH terminal.
- ztermy is a personal tool for technical and expert users. Default workflows
  must favor directness, mainstream conventions, and low friction. Keep
  necessary safety mechanisms transparent and contextual; never expose
  internal security architecture as concepts the user must manage unless a
  concrete risk or explicit user choice requires it.
- Netcatty and other SSH tools are product references only.
- ztermy owns and evolves one built-in, provider-backed terminal Agent. Never
  integrate, detect, launch, bridge, or expose Codex, OpenCode, Claude Code, or
  any other external Agent/harness runtime. Their public product behavior may
  be studied as UX reference only.
- This external-Agent exclusion is a permanent product boundary, not deferred
  work. Do not add it to a future roadmap, prototype it behind a feature flag,
  retain dormant adapters, or introduce extension points whose purpose is to
  host those runtimes.
- Future AI work must deepen ztermy's own provider-backed assistant only.
  Competitor research, protocol similarity, or provider/model support must
  never be interpreted as permission to reconsider an external Agent runtime.
- AI tools are scoped to the terminal tab that owns the assistant sidebar. Do
  not add cross-terminal session enumeration, selection, or control.
- Do not copy third-party source code, images, icons, themes, or branding unless
  an explicit compatible dependency decision is recorded.
- Do not add a license file until the owner selects a project license.

## Architecture

- Keep domain logic independent from Qt UI types where practical.
- QML owns presentation and lightweight interaction glue.
- C++ owns application state, platform integration, I/O, security boundaries,
  persistence, and terminal state.
- External I/O must not block the GUI or Qt Quick render thread.
- A terminal viewport is one custom item; never model terminal cells as QML
  object trees.
- Windows-native behavior belongs behind a platform abstraction.

## Build and code quality

- Use C++23, MSVC, CMake, and Ninja.
- Prefer target-scoped CMake properties and imported targets.
- Treat the compilation database as the source of truth for clangd.
- All commits must follow the Conventional Commits specification.
- Add tests with behavior changes.
- Never log passwords, passphrases, private key content, terminal input, or
  unredacted secret-bearing command lines.
- Record significant technical choices under `docs/adr/`.

## Verification

- Configure and build through CMake presets.
- Run formatting, static analysis, unit tests, and focused runtime checks before
  declaring work complete.
- UI and platform behavior require runtime evidence; unit tests alone are not
  sufficient for Snap Layouts, IME, DPI, transparency, or terminal latency.
