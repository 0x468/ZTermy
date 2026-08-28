# ADR 0113: Separate static and dynamic Windows artifacts

## Status

Accepted

## Context

ztermy has a locally built static Qt release and an official dynamically linked
Qt toolchain suitable for reproducible CI builds. Publishing files with the
same stem obscures their runtime and support differences. The installer and
portable archive must also use the same tested deployment tree within one
flavor.

## Decision

- `msvc-static-release` produces `static`-qualified MSI and portable artifacts.
- `msvc-dynamic-release` produces `dynamic`-qualified MSI and portable
  artifacts from one Qt deployment component.
- Dynamic deployment normalizes the executable, DLLs, `qt.conf`, plugins, and
  QML modules into an isolated root that is runtime-smoke-tested before either
  package is accepted.
- WiX MSI contracts are flavor-aware. Static packages reject runtime DLLs;
  dynamic packages require the Qt and OpenSSL runtime boundary.
- Tag-triggered CI may prepare the dynamic MSI and portable ZIP as a draft
  release. A locally validated static portable ZIP can then be attached before
  publication.
- Artifact names always include `static` or `dynamic`.

## Consequences

Dynamic packages are larger collections of files but can be reproduced by a
hosted Windows runner with official Qt binaries. Static packages remain a
local release responsibility until the custom Qt toolchain is reproducibly
available to CI. The dynamic MSI currently assumes the Microsoft Visual C++
Redistributable is present; a future bootstrapper or app-local runtime decision
must close that clean-machine prerequisite explicitly.
