# SFTP and file-transfer research

Status: product and architecture baseline for V1.6, 2026-08-02

## NetCatty reference audit

The installed NetCatty application and the clean local source tree at
`D:\tmp\Netcatty` were inspected as behavioral references. No source, asset,
icon, theme, or branding is incorporated into ztermy.

Observed behavior in the live SSH window:

- The terminal toolbar's **Open SFTP** action opens a single remote browser on
  the left and reflows the terminal into the remaining space. The panel is not
  a floating modal and can coexist with terminal-local controls.
- The remote pane uses a compact breadcrumb/path row and icon toolbar. The
  observed controls include path bookmarking, follow-terminal-directory, copy
  path, list/tree view, filter, create operations, hidden files, refresh, and
  overflow actions. The list exposes Name, Modified, Size, and Type columns and
  a footer with item count and current path.
- The application's global **File transfer** button opens a task popover with
  All, Active, Queued, Paused, Needs attention, and Completed filters plus
  global pause/resume affordances. This is a queue view, not the remote browser.
- The source keeps the terminal side panel as a one-pane specialization of the
  richer SFTP view. `SftpSidePanel.tsx` mounts one pane and integrates terminal
  focus/current-directory behavior. `SftpView.tsx` owns the larger two-pane
  workspace. `SftpPaneToolbar.tsx` adapts its toolbar at compact widths.
- Directory state is explicit: connection, files, loading/reconnecting/error,
  selection, filter, filename encoding, hidden-file policy, and a mutation
  token. Transfer tasks separately track endpoint identity, direction, state,
  total/transferred bytes, speed, times, directory relationships, and retry
  eligibility. Conflicts separately carry source/destination metadata.

NetCatty's implementation is already a large subsystem with pane tabs,
bookmarks, virtual lists, tree view, drag/drop, remote editing, auto-sync,
compressed uploads, permissions, and extensive bridge code. Recreating that
entire surface in one milestone would obscure ztermy's more important first
decisions: thread ownership, credential lifetime, cancellation, safe partial
files, and deterministic task state.

## Comparable product patterns

- libssh2 exposes SFTP as a channel/subsystem managed through the
  `libssh2_sftp_*` API. In non-blocking mode, initialization and file operations
  may return `LIBSSH2_ERROR_EAGAIN`; callers must wait according to the session
  block directions and continue the same operation rather than treating it as
  a failure.
- WinSCP can move transfers to a background queue. Its background operations
  use separate server connections, have a bounded concurrency setting, and
  leave the interactive browser usable. This validates separating ztermy's
  browsing session from transfer workers.
- Termius markets SFTP as an integrated, simple transfer workflow rather than
  exposing protocol details. Tabby keeps an SFTP-tab workflow outside its core
  terminal surface through a plugin. Both support a focused initial scope
  instead of mixing terminal rendering and file-protocol state.

Primary references:

- <https://libssh2.org/libssh2_sftp_init.html>
- <https://libssh2.org/examples/sftp_nonblock.html>
- <https://winscp.net/eng/docs/transfer_queue>
- <https://winscp.net/eng/docs/ui_pref_background>
- <https://termius.com/pricing>
- <https://github.com/Eugeny/tabby>

## ztermy constraints

`Libssh2Session` currently owns one ztermy socket, one libssh2 session, the PTY
channel, and one auxiliary exec channel. `SshTerminalSession` advances all of
that state on one `std::jthread`. Sharing this session object with a second
SFTP thread would be a data race; advancing large transfers on the terminal
worker would instead create head-of-line blocking for terminal input/output.

An active terminal also must not retain a reusable plaintext password merely
so SFTP can reconnect. Saved sessions can re-resolve their credential through
the existing credential provider. Temporary sessions require a fresh prompt.
The SFTP UI receives only connection/task state and opaque identifiers.

Remote paths are POSIX paths regardless of the local Windows platform. They
must not be normalized with `std::filesystem::path` Windows rules. Local paths
remain native filesystem paths and all final local writes must stay inside the
exact path chosen by the user.

## Chosen V1.6 boundary

- One independent SFTP browsing connection per open terminal SFTP panel.
- Background transfer workers use independent authenticated connections and a
  bounded global scheduler; browsing never waits behind a file payload.
- One domain task model drives the controller, queue, persistence-free runtime
  state, and tests. Transfer records intentionally contain no secret.
- Listings and control operations use request/generation identifiers so late
  results cannot replace a newer directory after navigation or panel close.
- V1.6 transfers regular files. Directories remain navigable and manageable,
  but recursive directory transfer is deferred until traversal, symlink, and
  batch-conflict policies have dedicated coverage.
- Queue history is session-local in V1.6. Completed/failed entries may be
  dismissed; unfinished jobs are not silently resumed after process restart.

