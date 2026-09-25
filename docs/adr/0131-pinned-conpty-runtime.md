# 0131: Embed a pinned Microsoft ConPTY runtime

Status: Accepted (implementation and acceptance checks in progress)

## Evidence

The inbox Windows 11 ConPTY discarded both Kitty APC and Sixel DCS output from
a child with virtual-terminal processing enabled. Text and a completion marker
arrived. The same child under Microsoft's signed ConPTY 1.24.260710001 preserved
both sequences byte-for-byte. Direct engine/rendering tests could not detect
this transport boundary.

## Decision

Use the official MIT-licensed Microsoft.Windows.Console.ConPTY NuGet package,
pinned to 1.24.260710001 and an archive SHA-256. Independently verify the x64 DLL
and OpenConsole executable hashes, including local source overrides. This is an
explicit compatible dependency decision, not permission to copy Terminal UI,
themes or branding. The original copyright and MIT terms are included in
`resources/third-party/ConPTY-NOTICE.txt`.

Embed the DLL, host executable and notice in the application resource. Preserve
single-EXE distribution: no user installation, PATH edits, or network download
at runtime. Materialize a versioned, per-user cache before GUI construction;
later local sessions reuse the loaded API. Use cross-process extraction locking,
content verification, atomic replacement, absolute DLL loading with restricted
dependency search paths, and read-only pinned handles while in use. Keep the
upstream DLL and host adjacent. A load failure reports a local-session failure;
do not silently fall back to an incompatible inbox implementation.

The host is still a separately owned child process and must exit with its
session. This does not make the native console renderer the terminal engine:
Ghostty and ztermy continue to own the terminal state and presentation.

## Consequences

The executable grows by the compressed native payload (about 1.2 MB uncompressed).
The cache needs user write permission and is disposable only when no application
instance uses it. Different pinned versions use different directories; do not
delete other versions during startup. Initial x64 support matches the existing
release target; additional architectures require corresponding pinned binaries.

Required acceptance includes initial extraction and cache reuse, actual local
Sixel/Kitty rendering, fresh environment behavior, parallel host cleanup, terminal
startup latency, and static single-EXE launch without adjacent native DLLs.
These checks are not satisfied by the isolated transport probe alone.

Dynamic and static full-app local-image checks now pass. The static acceptance
copy contains only the EXE, without adjacent Qt or ConPTY DLLs. Transport tests
cover output, images, environment refresh, parallel close and wake events; a
native file-sharing test verifies the cached binaries cannot be opened for
writing while the application owns them. Startup profiling and cache-corruption
recovery under failure injection still require dedicated evidence.
