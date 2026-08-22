# ADR 0103: Native notification-area lifecycle

- Status: Accepted
- Date: 2026-08-22

## Context

ztermy may own long-running terminal and transfer sessions that should remain
available when the main window is dismissed. The default Windows close
behavior must remain unsurprising, while users who explicitly opt in need a
low-friction way to hide, restore, and fully exit the application.

The application is based on Qt Quick and deliberately does not otherwise
depend on Qt Widgets. Adding `QSystemTrayIcon` solely for this behavior would
pull a second UI stack into the executable.

## Decision

Add an application setting named **close to notification area**. It is off by
default. When enabled:

- ztermy publishes its existing application icon with `Shell_NotifyIconW`;
- closing the main window hides it and leaves application-owned work running;
- activating the icon restores and activates the main window;
- the native context menu offers Show/Hide and Exit;
- Exit removes the icon and performs an ordinary application shutdown;
- disabling the option immediately removes the notification-area icon.

The implementation remains behind `NativeWindow`, which is already the
Windows platform boundary. Smoke-test invocations ignore the persisted option
so automated window-close checks cannot be converted into hidden background
processes by a developer's local settings.

## Consequences

- The default close action is unchanged until the user opts in.
- Long-running work can remain available without a taskbar window.
- No Qt Widgets dependency or duplicate icon asset pipeline is introduced.
- A future non-Windows implementation can expose equivalent lifecycle intent
  through another platform adapter without leaking Win32 types into QML or
  application settings.
- Notification-area interaction still requires manual Windows runtime
  verification because unit tests cannot establish Explorer ownership or menu
  placement.
