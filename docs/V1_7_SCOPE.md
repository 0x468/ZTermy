# V1.7 session productivity scope

## Delivered contract

V1.7 turns the V1.6 SFTP and workbench foundation into a safer repeat-use
workflow:

- per-profile recent remote directories with a bounded MRU menu;
- URI-list drag and drop from Windows Explorer into the active remote directory;
- application-level transfer completion, failure, and cancellation notifications;
- a command-palette action for the global transfer center;
- persisted workbench page, side, width, composer height, and last remote path;
- visible session connection duration alongside the existing identity and status;
- existing shell-aware history parsing, current/global scopes, reusable scripts,
  composer, centralized actions, and customizable shortcuts retained as one
  coherent workflow.

The workspace document never contains credentials, private-key material,
terminal cells, terminal input, command history, or session output. Loading it
never opens a terminal or reconnects to a host.

## Deliberate boundaries

- Drag/drop uploads files only. Directory-tree upload needs an explicit traversal,
  symlink, conflict, and cancellation policy.
- Transfer notifications are in-app and non-blocking. Windows notification-center
  integration is not required for V2.
- Connection duration is safe session metadata. Remote CPU, memory, disk, and
  network sampling is promoted to the V1.8 observability design because it needs
  an independent channel and platform/shell capability negotiation; ztermy will
  not inject polling commands into the interactive shell.
- A workspace preference is restored only after the user explicitly starts a
  saved profile. Open panels, composer visibility, live sessions, and credentials
  are not restored.

## Automated evidence

- workspace state validation, atomic persistence, missing-file behavior, schema
  rejection, and MRU bounds have focused Qt tests;
- AppController integration and action registry behavior have focused tests;
- QML formatting and lint cover the recent-path menu, drag/drop surface, transfer
  toast, and session-duration presentation;
- the translation gate covers all new English and Simplified Chinese strings.

Runtime-only checks remain `PENDING` in
`docs/testing/DEFERRED_MANUAL_ACCEPTANCE.md`.

