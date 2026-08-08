# ADR 0037: Reference-first product-shell convergence

Status: accepted for V2.2

## Context

ztermy's native terminal, SSH, security, packaging, and Windows integration
reached a stable `0.2.1` baseline. The application shell nevertheless drifted
from the approved NetCatty UI/UX reference: Hosts and Settings accumulated
large headings, explanatory copy, and presentation cards while terminal-tool
workflows remained comparatively compact.

Feature-by-feature acceptance did not prevent this drift because it lacked a
frozen page anatomy and density contract.

## Decision

V2.2 treats confirmed NetCatty runtime workflow hierarchy and density as the
reference for the product shell. ztermy translates that hierarchy into owned
Qt Quick components and native Windows behavior. Runtime screenshots and
written anatomy are acceptance inputs; third-party implementation and assets
are not.

The reference is authoritative for task order, action grouping, panel
coexistence, progressive disclosure, and density. ztermy remains authoritative
for architecture, branding, security, accessibility, platform integration,
supported scope, and global appearance policy.

Unsupported reference modules are omitted. Settings remains a ztermy work tab,
Serial/AI/cloud/remote editing remain excluded, and profile-owned appearance is
not introduced.

## Consequences

- UI work begins from a frozen runtime state and an explicit completion matrix.
- Functional tool pages prefer compact rows and quiet separators over large
  cards and explanatory page heroes.
- A visually similar control is not accepted if its keyboard, focus, state, or
  resize behavior differs.
- Reference-version changes do not silently change the milestone; the baseline
  must be updated explicitly.
- The native architecture and existing stable functionality are preserved even
  when the QML presentation is substantially reorganized.

