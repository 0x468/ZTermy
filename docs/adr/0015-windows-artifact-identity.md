# ADR 0015: Generate and verify Windows artifact identity

- Status: Accepted
- Date: 2026-07-30

## Context

The first static and dynamic executables had no Windows version resource.
Explorer therefore exposed empty product, version, description, and original
filename fields, while the application version was separately hard-coded in
C++. The executable and installer also had no ztermy-owned native icon.

V1 distribution needs a consistent identity across the running application,
the PE executable, the portable archive, and the MSI. The project should not
depend on a manually edited binary icon or on third-party visual assets.

## Decision

`project(VERSION)` is the single source of the application version. CMake
configures both a generated C++ version header and a Windows `VERSIONINFO`
resource from that value.

The native icon uses the same original green rounded-square terminal mark as
the application chrome. A small ztermy-owned C++ build tool deterministically
generates a multi-size ICO from geometric primitives. The generated icon is
embedded in the executable and supplied to WiX as the Installed Apps product
icon; no binary icon is stored in the repository.

A Windows-only QtTest contract opens the final `ztermy.exe` with Win32 version
and resource APIs. It checks the fixed numeric version, required string
identity fields, and loadable icon resource. Both dynamic and static
preflights build and run this test, so the portable and MSI payload inherit a
verified executable.

Code signing is not implied by this decision and remains a separate release
policy choice.

## Consequences

- Updating the CMake project version updates application, PE, archive, and MSI
  versions together.
- Explorer, Installed Apps, shortcuts, and task switching have a consistent
  ztermy-owned icon and product identity.
- MSVC builds require the Windows resource compiler already supplied by the
  Windows SDK.
- Changes to the icon generator or resource template are subject to the same
  formatting, static-analysis, and executable-contract gates as application
  code.
- A human still reviews icon clarity at Windows shell sizes because resource
  presence alone cannot establish visual quality.
