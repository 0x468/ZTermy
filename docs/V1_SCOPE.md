# ztermy V1 scope

Status: draft

## Product goal

V1 is a dependable personal SSH terminal for Windows 11. It should feel like a
native desktop application, launch without a developer environment, and remain
responsive during long-running local and remote terminal sessions.

Netcatty is the initial product reference for feature placement and the first
visual direction. The implementation remains native C++/Qt and may evolve into
an independent visual identity after the main workflows are stable.

## V1 priorities

1. Native window shell and custom title bar
2. Local PowerShell session through ConPTY
3. SSH password and key authentication
4. Strict known-host verification
5. Host vault with create, edit, copy, delete, search, and grouping
6. Multiple terminal tabs
7. Correct terminal input, selection, copy, paste, search, scrollback, resize,
   Unicode, CJK, and IME behavior
8. Stable configuration, logging, diagnostics, portable packaging, and
   installer behavior

## Native window requirements

- Fully custom visual title bar
- Windows 11 Snap Layouts from the custom maximize/restore button
- Native move, resize, resize cursors, maximize, restore, minimize, and shadow
- Correct work-area sizing when maximized
- Per-monitor DPI handling
- Dark and light window integration
- Rounded corners where supported
- Configurable application opacity
- Optional Windows 11 backdrop material, kept separate from content opacity

## Deferred

- AI assistant
- Cloud sync and accounts
- Plugin API
- Mosh, Telnet, and serial connections
- Multi-platform releases
- Full Netcatty feature parity

SFTP, split panes, port forwarding, snippets, and automation are added only
after the terminal and SSH foundations meet their acceptance criteria.

## Reference policy

Allowed:

- Observe workflows, feature discoverability, information hierarchy, and user
  expectations.
- Recreate common product behavior using independent code and original assets.
- Compare behavior against multiple SSH tools.

Not allowed:

- Copy source, stylesheets, icons, screenshots, themes, text, or branding.
- Depend on undocumented internal behavior as a compatibility contract.
- Claim pixel-perfect compatibility or affiliation.

