# ADR 0141: Configurable Explorer shell menus

- Status: Accepted
- Date: 2026-10-02

## Decision

The Windows integration settings page offers one Explorer entry or a one-level
Shell submenu. The default is a single entry using the application's default
local Shell. Only detected, available built-in local Shells are presented; SSH
profiles and arbitrary executable commands are not menu targets.

Application settings schema 43 adds `windowsIntegration`; the fixed schema-42
fixture verifies preservation of every unrelated setting and rewrite. Unsaved
page drafts do not update Explorer preferences.

The page uses explicit Apply, Discard, and Reset defaults actions. Apply saves
only Windows integration preferences; unrelated category drafts remain unsaved.
Reset defaults changes this page\'s draft only and requires Apply to persist.
Apply and Discard remain visible but disabled while the draft matches saved
preferences, including an unordered comparison of selected submenu Shells.

The app publishes a bounded 16-byte, versioned, non-secret menu snapshot on a
serial background worker. It is scoped to the current user and installation
directory. Portable and custom-data launches never publish installed-menu
snapshots. Explorer reads this snapshot without Qt, app launches, network I/O,
or Shell detection. Missing/malformed snapshots fall back to the single default
entry; unavailable selected Shells are omitted when snapshots are refreshed.
If no selected Shell remains available, the submenu keeps a default entry.

Classic and Windows 11 menus share the same native IExplorerCommand handler.
The installer owns classic verb and CLSID/InprocServer32 registrations. Typed
installer descriptor/manifest schema 5 declares the bundled DLL and validated
GUID, preserves schemas 1–4, and uses ownership, rollback, and uninstall logic.
The signed sparse identity remains the separate opt-in Windows 11 registration.
App settings never modify installer-owned registrations or certificate trust.

`--local-shell <built-in-id>` accompanies `--open-directory`. Launch IPC version 2
preserves that choice, accepts previous version-1 requests, and rejects unknown
Shell IDs, duplicate flags, or Shell-only requests. Explicit unavailable Shell
requests fail instead of silently switching Shells. Directory paths remain
literal argv/working-directory data and are never injected into shell input.

## Verification

Tests cover migration, malformed choices, IPC startup queuing and literal paths,
availability filtering, native subcommand enumeration/clone/reset, and absence
of nested submenus. Native registration, upgrade rollback, and uninstall tests
run only inside an owned Windows Sandbox. The settings-page smoke uses keyboard
navigation through the visible action buttons and screenshots to verify draft
cancellation, explicit save, draft-only reset, and isolation from other drafts.
