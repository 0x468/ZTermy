# ADR 0142: Native Shell icons and bounded terminal launches

- Status: Accepted
- Date: 2026-10-02

Explorer's root entry keeps the Ztermy icon; submenu children use the same approved
SVG masters as built-in Shell profiles. Build-time ICO generation embeds seven
multi-resolution resources into the existing native handler. Explorer requires
neither Qt nor Shell discovery, external icon files, or a running application.
The default Shell child uses a generic terminal icon; explicit PowerShell versions
share the PowerShell master. A neutral purple provides contrast on light/dark menus.

Launch IPC version 3 contains only validated non-secret connection metadata and
local credential-file paths. Versions 1 and 2 remain readable. Requests are bounded
to 64 KiB, queued during startup, and scoped to the same user/data-directory owner.
Main-window and detached launches share existing terminal session ownership; a
detached request does not itself activate a hidden main window. Native credentials
or host-key interaction may bring the main window forward.

SSH launches reuse the existing backend, keychain/credential vault, jump/proxy
profiles and host-key verification. Temporary targets remain transient. Credential
files are bounded local regular files, read off the GUI thread into sensitive byte
buffers. Contents never enter CLI arguments, IPC, logs, saved hosts or settings.
Missing credentials are requested through a shared-motion modal dialog with a
serialized request queue. Command-line and URL plaintext passwords are rejected.

Remote directories use POSIX single-argument quoting before an existing startup
command. No general execute-command switch, PuTTY registry compatibility layer,
external agent runtime, or host-key bypass is introduced. Windows remote shells
require a different future directory strategy rather than misleading portability.

Verification covers malformed/conflicting flags, IPC migration and exactly-once
delivery, hidden-main detached launches, credential cancellation/queueing, invalid
credential files, all seven extracted native icon resources and real loopback SSH
password/encrypted-key authentication with normal host-key confirmation, escaped
directory bytes, screenshots and non-persistence of temporary host profiles.
