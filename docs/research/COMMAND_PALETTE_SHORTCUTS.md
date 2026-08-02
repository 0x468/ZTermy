# Command palette and shortcut research

Date: 2026-08-02

## NetCatty source and UI observations

NetCatty defines default key bindings in one domain model and renders a
Shortcuts settings page grouped into Tabs, Terminal, Navigation, Application,
and SFTP. Clicking a binding enters a recorder; Escape cancels, modifier-only
presses wait for a real key, and each row can be disabled or reset.

Its action execution remains a large application-level switch. The entry named
`commandPalette` opens the same `QuickSwitcher` used for hosts, tabs,
workspaces, and local shells. This is a useful visual and navigation reference,
but it does not give ztermy the single action metadata and dispatch boundary
needed by future native menus and settings.

The V1.5 design therefore follows NetCatty's visible settings organization but
does not copy its React implementation or its macOS/SFTP/serial surface.

## Windows Terminal reference

[Windows Terminal's command palette](https://learn.microsoft.com/en-us/windows/terminal/command-palette)
is opened by `Ctrl+Shift+P`, displays executable actions, supports actions with
no key binding, and permits selected actions to be hidden from the palette.

[Windows Terminal actions and key bindings](https://learn.microsoft.com/en-us/windows/terminal/customize-settings/actions)
use stable action ids and keep bindings separate from action definitions. The
documentation also makes an important terminal-specific promise: unbinding an
application shortcut allows that key sequence to reach the command-line
application instead. Windows-key combinations can be reserved by the OS and
may never reach the app.

ztermy adopts stable ids, optional bindings, palette visibility, and unbind
pass-through. It does not adopt JSON-authored arbitrary commands in V1.5.

## Qt reference

[QShortcut](https://doc.qt.io/qt-6/qshortcut.html) distinguishes normal
activation from ambiguous activation when a sequence overlaps another
shortcut. ztermy prevents exact duplicate effective bindings before they reach
that ambiguous runtime state. Key sequences are normalized with
`QKeySequence::PortableText` so persisted values are independent of translated
display labels.

## Decision summary

- C++ owns descriptors, validation, overrides, availability, and authorization.
- QML owns the palette, recorder, focus flow, and view-level execution glue.
- Exact conflicts are rejected; empty bindings are valid and pass through.
- Single unmodified printable keys are rejected because ztermy is a terminal.
- V1.5 uses one Windows scheme and one key sequence per action.
- The palette searches localized name, description, category, id, and shortcut.
- No password, terminal input, command history, or secret-bearing content enters
  registry logs or settings.
