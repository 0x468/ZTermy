# ADR 0078: AI responsive and accessibility runtime contract

Status: accepted

## Context

The terminal AI surface combines ordinary actions, checkable tool buttons,
editable text, context disclosure, and a resizable workbench. QML lint and unit
tests cannot prove the roles exposed by the real Qt accessibility bridge or that
the panel remains usable after Windows resizes the native window.

## Decision

- Give the AI launcher, core actions, context disclosure, prompt editor, and
  workbench pane stable object names and explicit accessible names.
- Validate ordinary actions as `Button`. Validate checkable tool buttons as the
  Qt bridge's checkable `CheckBox` role (while accepting `Button` on compatible
  Qt accessibility backends). Validate the prompt as `EditableText`, context
  disclosure as `Button`, and the workbench as `Pane`.
- Extend the real-window layout smoke with AI captures in dark regular,
  dark 500 x 360 compact, and light regular modes. The compact panel must remain
  visible, have positive width, and remain inside the native window.
- Keep Windows foreground activation out of the layout contract. Keyboard focus
  order and restoration belong to the dedicated keyboard smoke because another
  desktop process may legitimately own foreground activation during automated
  layout capture.
- Emit a content-free accessibility contract artifact beside screenshots. It
  contains only pass flags plus role/name metadata from fixed test controls.

## Consequences

- Role/name regressions and narrow-window clipping fail a real Qt window test,
  not only a QML source inspection.
- The evidence artifact contains no prompt, response, terminal, provider,
  credential, or MCP content.
- Screen-reader usability still requires the release-candidate keyboard and
  Windows accessibility manual matrix; this contract is necessary but not a
  substitute for human assistive-technology validation.

