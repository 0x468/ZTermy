# V1.1 NetCatty UI/UX audit

Status: baseline in progress

## Evidence and policy

The initial runtime audit used the locally installed NetCatty `1.1.73` and the
accepted ztermy `0.1.0` static Release candidate on Windows 11. Source
inspection was limited to understanding component boundaries, state behavior,
and timing; third-party implementation and assets are not copied.

Runtime observations are translated into ztermy requirements. A reference
feature that ztermy does not support is omitted rather than represented by an
empty navigation item.

## Global chrome

### NetCatty observations

- One compact top row combines root surfaces, sessions, new-tab affordance,
  utility commands, and window controls.
- The visual title bar is approximately 36 logical pixels high. Window-control
  hover fills the complete chrome height even though the icon row is smaller.
- Root tabs, session tabs, and the add action read as one continuous work-tab
  strip.
- A new session enters from the left over roughly 220 ms.
- Active sessions carry an icon, label, connection-status dot, and close
  action without a separate navigation rail.

### ztermy baseline

- Hosts, terminal sessions, the add action, and native caption buttons already
  share the title row.
- Terminal pages already release the full content area to the terminal; the
  Hosts navigation rail is not persistent there.
- The 42-pixel title row is more spacious and the active-tab surface is
  stronger and more rectangular than the reference.
- Session state and the close action work, but tab entry/exit and overflow do
  not yet have the same spatial continuity.

### V1.1 requirements

- Keep the existing native title-bar architecture and hit testing.
- Tighten tab geometry and active/hover hierarchy without reducing the native
  caption-button target.
- Add reduced-motion-aware tab lifecycle feedback.
- Keep the add action attached to the last work tab in every empty/non-empty
  state.
- Add an overflow strategy before tab labels become unusable.

## Hosts

### NetCatty observations

- The host surface is a browse-and-connect dashboard rather than a CRUD table.
- A prominent search/quick-connect field and connect action lead the page.
- New-host creation is a primary split action; serial is a separate action.
- Recent connections appear before the complete host collection.
- Hosts are shown as compact cards in a responsive grid with identity and
  endpoint metadata.
- Edit is secondary; management commands are not four equally weighted
  buttons on every card.
- The page-specific navigation rail is about 210 logical pixels and supports a
  clear selected state plus a bottom Settings entry.

### ztermy baseline

- The 210-pixel rail and selected-state foundation already exist.
- Search, groups, profile validation, connect, edit, copy, and delete are
  functional and keyboard accessible.
- Hosts are presented as full-width rows. Connect, Edit, Copy, and Delete have
  similar visual weight, so record management dominates scanning.
- There is no recent-connections section or direct endpoint quick-connect
  surface.

### V1.1 requirements

- Make search and connecting the first visual task.
- Add a responsive recent-connections region using local, non-secret profile
  metadata.
- Use responsive host cards at regular widths and a compact list at narrow
  widths.
- Keep Connect directly discoverable; move Edit, Copy, and Delete into a
  secondary menu or revealed action region that remains keyboard accessible.
- Preserve groups and security-sensitive confirmations.
- Do not add the reference Serial action.
- Do not add navigation for unavailable NetCatty modules.

## Terminal workspace

### NetCatty observations

- The terminal consumes almost the complete client area below the top work-tab
  row.
- A thin information/command row shows connection identity and compact live
  metadata, with terminal utilities aligned to the right.
- Secondary information progressively disappears at narrow widths.
- The terminal surface is visually quieter than the host-management surface.

### ztermy baseline

- The terminal already consumes the full remaining workspace.
- A thin status row communicates connection state, encoding, language level,
  and search shortcut.
- Search, selection, paste confirmation, scrollbar, IME, alternate screen,
  resize, and session recovery are established.
- The status row is informative but does not yet establish a clear left
  identity cluster and right action cluster.

### V1.1 requirements

- Recompose the row into connection identity/state on the left and terminal
  actions on the right.
- Keep performance or diagnostic data optional and progressively hidden.
- Do not animate the viewport, glyphs, cursor, scrollback, or resize geometry.
- Preserve terminal background/material independence.

## Settings

### NetCatty observations

- Settings opens as a separate approximately `980x720` window.
- A compact left category rail and independently scrolling content area replace
  the main-window top tabs.
- Application, Appearance, Terminal, Shortcuts, SFTP, AI, Sync, and System are
  categories in the audited build.
- Settings sections use quiet bordered cards, row-oriented label/description
  hierarchy, and controls aligned to the right.
- Appearance exposes language, UI font, whole-window opacity, theme mode,
  theme choices, accent override, icon choices, and host-vault preferences.

### ztermy baseline

- Settings is a root surface inside the main window and shares the Hosts rail.
- Appearance and terminal preferences are already global and material-aware.
- The current page uses large stacked section cards and explicit Apply,
  Discard, and Reset actions.

### Open decision

An independent Settings window improves separation from terminal work and
matches the reference model, but adds a second native-window lifecycle,
appearance synchronization, DPI, persistence, focus, Snap/resize, and testing
surface. Keeping Settings in the main window is simpler and preserves the
current tab model.

This choice is intentionally not made by the audit. It requires owner review
before the Settings implementation stage.

## Interaction and motion

### Reference timing observed in runtime/source

| Interaction | Reference behavior |
| --- | --- |
| New work tab | 220 ms fade plus 12 px horizontal entry |
| Lazy content | 160 ms fade plus 2 px vertical entry |
| Theme change | 220 ms color/view transition |
| Ordinary hover | approximately 120-150 ms color/border change |
| Collapsible/panel content | approximately 200-220 ms |
| Reduced motion | transitions become effectively immediate |

### ztermy V1.1 translation

- Use shared QML motion tokens rather than page-owned durations.
- Keep hover geometry stable; page or tab entry may translate only while it is
  being introduced.
- Use opacity and short spatial transitions to explain hierarchy, not as
  decoration.
- Interruptions and rapid repeated actions must settle at the newest state.
- Focus, accessible names, and action availability must be correct before an
  animation begins and after it is disabled.

## Typography and identity

- NetCatty uses its own branding and Mona Sans-based interface.
- ztermy keeps Segoe UI Variable for a Windows-native identity and keeps its
  original icon system.
- ztermy keeps green as the default accent. A later global Follow Windows or
  Custom accent source may derive a contrast-safe palette.
- Alignment targets font size, weight hierarchy, line height, and density, not
  the third-party font or logo.

## Initial gap priority

1. Hosts dashboard composition and action hierarchy.
2. Work-tab geometry, overflow, and lifecycle motion.
3. Terminal information/action row.
4. Settings presentation decision and subsequent convergence.
5. Host editor, prompts, dialogs, empty/error states, and micro-interactions.
6. Final theme, DPI, keyboard, reduced-motion, and performance evidence.

## Acceptance evidence to retain

- NetCatty observations recorded as text only; reference screenshots remain
  local research and are not shipped.
- ztermy-owned dark and light screenshots at regular and narrow widths.
- ztermy-owned work-tab lifecycle and dialog/panel motion recordings.
- Reduced-motion comparison.
- Keyboard focus sequence for Hosts, host editing, Settings, and dialogs.
- Runtime evidence for Snap Layouts, DPI, IME/CJK, resize, terminal latency,
  and large output after convergence.
