# Explicit desktop taskbar identity

Status: Accepted

Ztermy's sparse MSIX package exists only to register the Windows 11 Explorer
command. A terminal launched by that packaged COM surrogate can inherit its
package application identity even though its HWND contains the correct icon.
The taskbar then resolves the hidden extension application instead of Ztermy.

Use the version-independent desktop AppUserModelID `ZSeries.Ztermy` before UI
creation, and assign the same ID explicitly to main and detached windows.
Each window supplies a relaunch icon from the installed executable's native
icon resource and a plain executable relaunch command. Never persist the
original command line, SSH credentials, or directory arguments in taskbar
properties. This does not alter package registration, certificate trust, or
terminal launch semantics. A future shortcut writer must use this same ID.

Reference: [Microsoft AppUserModelIDs](https://learn.microsoft.com/en-us/windows/win32/shell/appids).
