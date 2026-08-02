# ztermy V1.6 scope

Status: engineering implementation complete on 2026-08-02; owner-visible acceptance deferred

## Goal

V1.6 delivers a useful terminal-attached SFTP browser and a background file
transfer foundation. It follows the interaction hierarchy observed in
NetCatty without copying its source or assets: an SSH terminal opens one
reflowing remote-file side panel, while application-wide transfer activity is
presented in a compact queue surface.

The milestone is deliberately smaller than a complete dual-pane file manager.
It establishes transport ownership, credential lifetime, cancellation,
conflict handling, and task-state contracts that later file-management and
remote-editing features can reuse.

## Included

1. Add a ztermy-owned non-blocking SFTP transport over the pinned libssh2
   dependency. No libssh2 type crosses the infrastructure boundary and no
   operation blocks the GUI, Qt Quick render thread, or interactive terminal
   worker.
2. Give the browser and background transfers independent SSH/SFTP sessions.
   They reuse endpoint, host-key verification, authentication policy, and
   credential-provider boundaries, but never share a mutable libssh2 session
   with the interactive PTY.
3. Open SFTP from the active SSH terminal toolbar, the command palette, or its
   configured shortcut. The side panel participates in the existing terminal
   workbench layout, side choice, resizing, focus routing, and compact-mode
   rules.
4. Present one remote directory with breadcrumb/path navigation, parent,
   refresh, filtering, hidden-file visibility, sortable Name/Modified/Size/Type
   columns, loading/error/empty states, multi-selection, and keyboard access.
5. Support core remote operations: create directory, rename, and delete. Treat
   symbolic links as distinct entries and never recursively follow them.
6. Support file upload and download through native file/directory pickers.
   Transfers run outside the browsing connection and report queued, running,
   needs-attention, completed, failed, and cancelled states with byte progress,
   speed, and safe cancellation.
7. Resolve destination conflicts explicitly with Skip, Replace, Rename, and
   Cancel. The decision surface shows source and destination metadata. Batch
   apply-to-all is allowed only for compatible conflicts and is never silently
   selected.
8. Keep partial downloads under a temporary name and publish the destination
   only after successful completion. Cancellation and failure remove owned
   partial files without deleting a pre-existing destination.
9. Reuse saved-profile credentials from the configured vault only inside C++.
   A locked vault or a temporary/password connection that cannot be reopened
   transitions to needs-attention and requests credentials; secrets are never
   stored in QML, transfer records, status text, or logs.
10. Add stable SFTP and transfer actions to the V1.5 Action Registry only when
    their backing operation is available. Keep English and Simplified Chinese
    complete and add accessible names, visible keyboard focus, themed
    tooltips/popovers, reduced motion, and usable compact/DPI layouts.
11. Add domain, transport, controller, persistence, runtime UI, real-host, and
    package evidence. The existing password host at `192.168.1.25` and the
    configured `testkey` Ed25519 identity are manual/opt-in integration gates;
    credentials and private material never enter source or test output.

## Initial action surface

- Terminal: toggle SFTP side panel and focus the SFTP browser.
- SFTP browser: focus path/filter, go to parent, refresh, toggle hidden files,
  upload files, download selected files, create directory, rename, and delete.
- Application: open the transfer queue and cancel/retry eligible tasks.

Default bindings are assigned only after conflict review against the terminal
and existing action registry. Unbound actions remain command-palette and
pointer accessible without consuming terminal input.

## Product alignment

- Match NetCatty's terminal-side single remote pane: compact breadcrumb,
  icon-only toolbar with themed tooltips, list columns, status footer, and
  viewport reflow rather than an overlay.
- Match its separation between browsing and a global transfer queue, but expose
  only task filters and controls backed by real V1.6 states.
- Follow WinSCP's mature background-transfer principle: queued work uses
  separate connections so browsing stays responsive. V1.6 limits concurrency
  and does not equate more tasks with unbounded SSH connections.
- Preserve ztermy's native QML design system, Action Registry, settings work
  tab, host trust policy, and credential providers.

## Deferred

- A standalone dual-pane local/remote file manager, pane tabs, bookmarks,
  tree view, follow-terminal-directory shell integration, and drive browsing.
- Recursive directory upload/download, drag and drop, clipboard upload,
  permissions/owner editing, remote text editing, file associations, file
  watching/auto-sync, compressed upload, and external-editor bridges.
- Transfer pause/resume and restart checkpoints. V1.6 cancellation is safe but
  a retry starts a new transfer from byte zero.
- Proxy/jump-host SFTP until the SSH transport has a shared production proxy
  boundary. Serial and third-party branding/assets remain outside the product.

## Acceptance

V1.6 engineering completion requires formatting, static analysis, Dynamic
Debug and Static Release builds/tests, package-contract checks, and synthetic
cancellation/conflict fault injection. Those gates and their evidence are
recorded under `docs/testing/V1_6_SFTP_TRANSFERS.md`.

Real-window behavior, an opt-in real-host SFTP pass, and owner-performed
keyboard/mouse acceptance remain product-acceptance gates. They are explicitly
deferred—not silently waived—under
`docs/testing/DEFERRED_MANUAL_ACCEPTANCE.md` while GUI and real-host operation
are not authorized.
