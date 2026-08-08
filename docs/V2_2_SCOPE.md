# V2.2 NetCatty workflow convergence

Status: implementation candidate for the `0.2.2` line; automated gates passed,
manual acceptance remains open

## Objective

V2.2 corrects the product-shell drift identified after the accepted `0.2.1`
release. It converges ztermy on the current NetCatty workflow hierarchy,
information density, and interaction anatomy while preserving ztermy's native
Qt/C++ implementation, owned visual identity, and explicitly narrower product
scope.

This is not a source port and it is not a feature-count exercise. Reference
source is inspected only to understand component boundaries and state
transitions. No third-party code, assets, themes, text, or branding are copied.

## Frozen reference baseline

The implementation is evaluated against the locally installed NetCatty
`1.1.75` application, with its offered `1.1.76` update, and the local reference
source under `D:/tmp/Netcatty`. The binary and source may not be byte-identical;
only behavior confirmed by runtime evidence or consistent source structure is
treated as a requirement.

The retained runtime views are:

- Vault/Hosts, including the top command bar and New Host side inspector;
- a connected terminal with its compact identity/action toolbar;
- terminal SFTP, history, scripts, composer, find, and overflow surfaces;
- the dedicated Settings window categories and compact row anatomy.

Reference screenshots remain local research evidence and are never packaged.

The detailed as-is/reference/decision matrix is maintained in
`NETCATTY_COMPARISON.md`. This scope remains the normative boundary; the matrix
records current parity and residual gaps.

## Approved ztermy differences

- Settings remains an on-demand singleton work tab in the main native window.
- Serial, AI, cloud sync, collaboration, and remote editing remain excluded.
- Window and terminal appearance remain global rather than profile-owned.
- ztermy retains its own brand, icons, Segoe UI Variable interface typography,
  Windows materials, accent system, and native caption behavior.
- Windows Credential Manager and the portable encrypted vault remain the
  credential boundary.
- Unsupported modules are omitted instead of represented by inert navigation
  or toolbar placeholders.

## Required convergence

### Product shell and Hosts

- The Hosts root becomes a compact browse-and-connect workspace rather than a
  page-title-and-card dashboard.
- Search and quick connect share the primary command row with Connect, view,
  organization, and New Host actions.
- Recent and saved hosts share a compact item anatomy. The whole item connects;
  edit and management actions are secondary hover, keyboard, or menu actions.
- Recent and group sections remain collapsible and persisted.
- The right-side host inspector continues to resize the host collection and
  uses compact field sections rather than a modal or presentation card.
- Group suggestions remain editable and keyboard-correct. Nested groups and
  tags are separate model work and must not be faked by UI-only state.

### Terminal workspace

- The toolbar keeps identity and connection state on the left and terminal
  actions on the right, following the reference order where supported.
- SFTP, composer, find, session logging, scripts/command snippets, and overflow
  may coexist without covering the viewport or duplicating actions.
- History is available through overflow and the workbench; it preserves current
  profile/global scopes, count, refresh, keyboard run/insert, and save actions.
- The existing command library is labelled as command snippets until a true
  triggerable script runtime exists.
- The workbench remains movable and resizable with an interaction-safe resize
  boundary.

### Settings

- Application/About may retain the brand and release identity presentation.
- Appearance, Terminal, Shortcuts, Security, and SFTP use compact setting rows
  with labels on the left and controls on the right at regular widths.
- Language and interface font belong to Appearance, matching the reference
  taxonomy; Application is reserved for product, release, update, startup, and
  diagnostic information.
- SFTP receives a visible category only for preferences backed by persisted
  behavior. System remains deferred until it owns real settings.
- Settings preserves keyboard navigation, independently scrolling details,
  Apply/Discard semantics, live appearance preview, and narrow reflow.

### Integrated SFTP and transfers

- The browser uses a compact breadcrumb/path and adaptive action toolbar.
- Home, parent, recent paths, refresh, upload, new folder, filter, hidden files,
  list columns, parent entry, rename, delete, and download remain functional.
- Path bookmarks, terminal-directory locate/follow, copy path, directory-first
  sorting, richer columns, and overflow behavior are implemented only with
  persisted and tested state.
- Permission and transport errors remain in the file surface without replacing
  the last successfully viewed directory.
- Transfer cancellation, retry, dismiss, and clear-completed actions remain
  state-stable while progress updates arrive. Unsupported pause/resume actions
  are not shown.

## Density contract

- Ordinary toolbars and setting rows target 28-36 logical pixels.
- Host items target 64-72 logical pixels at regular widths.
- Functional pages do not start with explanatory hero text.
- Large `SectionCard` presentation is limited to About/release identity, empty
  states, destructive boundaries, and content that genuinely benefits from a
  grouped container.
- Hover and focus never change outer geometry. Ordinary feedback completes in
  at most 150 ms; coordinated drawer/tab transitions complete in at most
  220 ms and honor the Windows reduced-motion preference.

## Completion evidence

V2.2 is complete only when all of the following exist:

1. ztermy-owned dark and light screenshots for regular and narrow Hosts,
   host-editor, terminal-workbench, Settings, and SFTP states;
2. keyboard-only evidence for the command row, host items, side inspector,
   workbench tabs, file list, transfer actions, and Settings categories;
3. QML lint, translation verification, formatting, clang-tidy, static Release
   build, and every non-network automated test passing;
4. retained terminal rendering, IME, resize/reflow, Snap Layout, shutdown, and
   transfer-cancellation runtime gates;
5. a manual acceptance document listing real-host SFTP and visual checks that
   cannot be proved safely in automation.
