# NetCatty and ztermy product comparison

Status: V2 product comparison retained; AI boundary superseded by V3 program

## Purpose and evidence

This document prevents reference drift. It compares the locally installed
NetCatty `1.1.75` runtime, the offered `1.1.76` update, the reference source at
`D:/tmp/Netcatty`, and ztermy `0.2.8`. Runtime behavior wins when
the installed binary and source snapshot differ.

The source review identified the following reference boundaries:

- `components/vault/VaultHostListSection.tsx`,
  `components/HostDetailsPanel.tsx`, and `components/QuickConnectWizard.tsx`
  own the host browsing, editor, and quick-connect flows;
- `components/terminal/TerminalToolbar.tsx` and the terminal-layer components
  own session identity, inline actions, and terminal-adjacent panels;
- `components/sftp/SftpPaneView.tsx`, `SftpPaneToolbar.tsx`,
  `SftpBreadcrumb.tsx`, and the SFTP hooks own the integrated file browser;
- `components/SettingsPage.tsx` and `components/settings/tabs/*` own settings
  taxonomy and category content;
- Electron bridges and application state hooks separate renderer interaction
  from SSH, SFTP, session logging, persistence, and transfer work.

These files are research evidence, not dependencies. ztermy does not copy
their source, styling, assets, strings, or branding.

## Executive finding

The earlier ztermy shell had drifted in three ways: Hosts behaved like a
dashboard, settings used large presentation cards for ordinary preferences,
and terminal tools were added one feature at a time without preserving the
reference action hierarchy. V2.2 corrects those structural problems.

The V2 release aligns with NetCatty on the main task flow and information
density, but it is not feature-complete relative to NetCatty. The largest
remaining gaps are remote system telemetry, advanced SFTP navigation, a true
script runtime, terminal keyword highlighting/recording/encoding, and richer
transfer controls. Some are deferred; Serial, AI, cloud sync, collaboration,
and remote editing are explicitly excluded.

Later V2 milestones closed the listed telemetry, SFTP, script, terminal, and
transfer gaps through V2.14. The owner has now selected AI integration for V3;
`V3_AI_PROGRAM.md` and `research/AI_TERMINAL_LANDSCAPE.md` supersede this
document's V2-only AI exclusion. Serial, cloud sync, collaboration, and remote
editing remain excluded.

## Detailed matrix

| Area | NetCatty reference | ztermy 0.2.8 | Decision / remaining gap |
| --- | --- | --- | --- |
| Product shell | Persistent work tabs, compact title chrome, global utility actions, and terminal tabs share one task hierarchy. | Hosts, Settings, and terminal sessions share the custom native tab bar; Settings is an on-demand singleton tab. | Aligned in workflow. ztermy retains native Windows caption/Snap behavior and its own brand. |
| Startup | Opens into the host/vault workflow rather than forcing an unsolicited shell. | Opens Hosts and does not create a local terminal automatically. | Aligned. Session restoration remains later work. |
| Hosts root | Dense vault/tree workspace with search/quick connect and New Host actions; no explanatory dashboard hero. | One compact command row combines filtering or `user@host[:port]`, Connect, local Terminal, and New host. | Corrected in V2.2. |
| Host items | Compact recent/grouped entries; item activation connects while management actions are secondary. | Recent and saved entries are compact, grouped, collapsible, persisted, keyboard-activatable, and use secondary edit/more actions. | Main anatomy aligned. Nested groups, tags, bulk organization, and additional protocols are absent. |
| Host editor | Right-side `HostDetailsPanel` reduces the host-list width; fields are organized as an inspector rather than a modal card. | New/Edit host uses a fixed right inspector and keeps the list visible. Generated profile name, group suggestions, credentials, Escape, and focus restoration are retained. | Main interaction aligned. NetCatty's richer advanced host options are deferred unless an SSH use case justifies them. |
| Credentials | Vault state is integrated into the connection workflow. | Installed builds use Windows Credential Manager; portable builds use the encrypted portable vault, with visible lock state and unlock entry points. | Deliberate native/security difference. No plaintext fallback. |
| Terminal identity row | Compact session identity/state on the left; CPU, memory, disk, network, and latency values expose richer hover details. | Compact identity plus bounded Linux CPU, memory, root-disk, network, and auxiliary SSH latency metrics; keyboard/click/hover details show recent trends, cores, processes, mounts, and interfaces. | V2.6 aligned at the product level. ztermy deliberately polls only the visible active tab, gives history priority, and suspends bounded failures; macOS/remote Windows collectors remain later adapters. |
| Toolbar order | Keyword highlight, SFTP, composer, find, session log, scripts, then overflow; unsupported/less-used actions progressively move into overflow. | Keyword highlight, SFTP, composer, find, session log, command snippets, recorder state, and overflow follow the same task hierarchy; lower-priority actions progressively move into overflow at narrow widths. | V2.7 aligns the supported action hierarchy without adding inert controls. |
| Terminal side panels | File transfer, scripts, history, theme, system information, notes, and AI can occupy a movable terminal-adjacent surface. | SFTP, command history, command snippets, search, and composer use one movable/resizable workbench. | Correct shared-panel model. Theme/system/notes are deferred; AI is excluded. |
| Command history | Search, current-host/global scope, counts, refresh, and command reuse actions. | Search, current-profile/global scope, counts, refresh, Run, Insert, and Save as snippet are implemented. | Functionally aligned for supported shells. History adapters and large-history performance need broader real-world coverage. |
| Scripts | Reusable scripts include metadata, recording, and execution-oriented behavior. | Command snippets remain reusable single commands. Structured recording captures only commands run from composer/history/snippets, supports pause/review/JSON export and timed replay, and never records raw terminal input. | V2.7 establishes a safe recorder foundation. Variables, triggers, persistent script management, and a general runtime remain deferred. |
| Composer | Dedicated bottom input; Enter sends and Shift+Enter inserts a line break; nearby reusable commands accelerate entry. | Same keyboard contract and terminal focus restoration. | Aligned. |
| Find | Compact terminal search integrated with the toolbar. | Toolbar find with keyboard route and close/focus restoration. | Aligned at core level; advanced match/navigation polish remains possible. |
| Session logging | Manual logging is a first-class toolbar action with generated session-oriented names and backend state. | Start/stop logging is integrated and produces a generated `.log` path. | Core behavior aligned; export formats and richer log management are later work. |
| Keyword highlighting | Per-host rules can emphasize matching terminal text. | Saved SSH profiles own enabled literal rules and colors; quick connections keep rules session-local. Matching is bounded to the visible snapshot, supports wide characters and case policy, and gives the first rule priority. | V2.7 aligns the workflow while deliberately excluding unbounded regular expressions from the paint path. |
| Session terminal settings | Per-session terminal controls and encoding are reachable from terminal actions. | Temporary font, size, ligature, opacity, cursor, and default-color overrides are discarded with the tab. SSH encoding switches real transport conversion between UTF-8 and GB18030. | V2.7 aligned for the supported contract; global defaults remain the only persistent appearance policy. |
| SFTP opening path | Integrated SFTP resolves the remote user's home directory and remains attached to the terminal session. | Opens the remote home directory and shares the terminal workbench. | Aligned; the read-only real-host GUI smoke proves the visible toolbar-to-home-listing path. |
| SFTP navigation | Home, parent, breadcrumb/editable path, bookmarks, terminal-directory locate/follow, copy path, list/tree modes, filter, hidden files, refresh, overflow, and `..`. | The same core navigation is implemented, including event-driven terminal CWD locate/follow and a lazily loaded tree. Bookmarks, list/tree mode, follow preference, sort order, visible columns, directory-first order, and filename encoding are persisted per host workspace. | Core workflow aligned. Breadcrumb segments and shell-provided terminal-directory availability remain possible refinements. |
| SFTP file actions | Upload, download, new folder/file, rename, delete, drag/drop, context actions, encoding, and detailed columns/sorting. | Upload, download, new folder/file, rename, delete, drag upload, completed-download drag-out, UTF-8/GB18030 filename conversion, configurable name/time/size/type columns, sorting, and permission-preserving errors are implemented. | Daily-use workflow aligned. Direct virtual drag of a remote file before downloading and recursive directory transfer remain deferred backend contracts. |
| SFTP errors | Errors remain contextual and do not unnecessarily destroy useful navigation state. | A permission/transport error is shown while retaining the last successful listing and recovery controls. | Corrected from the former full-surface dead end; destructive and permission-error recovery still need manual acceptance. |
| Background work and shutdown | Renderer-side operations are mediated by Electron bridges and application state. | SFTP tree requests are bounded, deduplicated, invalidated by root generation, and deprioritized behind mutations; shutdown closes the command gate and cancels queued work. | V2.5 native reliability contract. Late UI work cannot prolong session teardown indefinitely. |
| Workspace recovery | Application state is persisted by the Electron application. | Non-secret workspace JSON uses atomic commits plus a last-known-good backup, restores from primary corruption, and refuses to overwrite a newer schema. | V2.5 deliberate native recovery policy. Credential stores remain separate and are never copied into workspace backup. |
| Transfers | Rich upload/download task rows, aggregate controls, per-task actions, progress, and completed-history management. | True byte-range pause/resume, persisted recovery, aggregate pause/resume/cancel, per-task retry/cancel/dismiss, copy path, open download folder, native drag-out of completed downloads, and clear-completed are implemented. | V2.8 closes the supported daily-use transfer surface. Recursive directory jobs and direct virtual remote-file drag are explicitly deferred. |
| Settings shell | Left category rail and compact right detail surface: Application, Appearance, Terminal, Shortcuts, SFTP, AI, Sync/Cloud, and System. | Application, Appearance, Terminal, Shortcuts, SFTP, and Security use the same compact rail/detail anatomy inside the Settings tab. | Main hierarchy aligned. Security is a ztermy-owned category; empty AI/cloud/system placeholders are intentionally omitted. |
| Settings density | Ordinary preferences are compact rows; cards are reserved for genuinely grouped or exceptional content. | Appearance, Terminal, Security, and SFTP use compact rows and Apply/Discard semantics; brand/release identity retains presentation treatment. | Corrected in V2.2. |
| Appearance model | Rich global and in some cases host/profile appearance controls. | Global UI/terminal appearance, accent policy, material, opacity, fonts, and language. | Deliberate global-only policy. Profile-owned appearance is excluded as unnecessary complexity. |
| Native window | Electron implements custom chrome through its own platform integration. | Qt/C++ implements custom chrome, native resize cursors, Snap Layout hit testing, work-area maximize, DPI, Acrylic/Transparent/Mica/Mica Alt. | ztermy-specific strength; retained as a non-reference requirement. |
| Terminal engine | xterm.js renderer and Electron/Node session bridges. | One custom Qt Quick `TerminalItem`, C++ terminal state, native ConPTY, libssh2, and Ghostty VT parsing. | Architecture intentionally differs; behavior and latency are the contract. |
| Accessibility/localization | Web accessibility and localized renderer strings. | Keyboard smoke, focus states, themed tooltips, English/Chinese Qt catalogs, native scaling, Windows high-contrast binding, and a physical Narrator/contrast/mixed-DPI matrix are enforced. | Automated contracts and the V2.3 manual evidence boundary are explicit; assistive-technology speech and physical monitor behavior are never claimed from unit tests alone. |

## Explicit exclusions

The following NetCatty areas do not belong to the current ztermy product scope:

- Serial, Telnet, and Mosh protocol surfaces;
- AI assistant and provider configuration in V2; these enter only through the
  separately scoped V3 AI program;
- cloud sync, collaboration, and account infrastructure;
- remote text editing and remote-development workspace features;
- inert placeholders for any unsupported module.

Port forwarding and terminal split panes are not implicitly excluded, but each
needs a separate product decision and native backend contract before entering a
milestone. Remote system telemetry now ships under the bounded auxiliary-channel
contract in ADR 0041.

## Acceptance rule going forward

For every reference-alignment change, record all four columns before coding:

1. confirmed NetCatty runtime behavior;
2. reference source boundary and state transition;
3. current ztermy behavior and technical constraint;
4. one of: align now, deliberate difference, defer with milestone, or exclude.

Visual similarity alone is insufficient. Pointer, keyboard, focus, resize,
error recovery, persistence, and shutdown behavior must be accepted together.
