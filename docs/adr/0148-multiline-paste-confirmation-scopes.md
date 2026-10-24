# ADR 0148: Multi-line paste confirmation scopes

- Status: Accepted
- Date: 2026-10-25

## Decision

The main and detached terminal windows share one paste confirmation surface.
Cancellation is initially keyboard-focused. Confirmation offers three scopes:
ask every time, suppress for this application run, or never ask again.

Only accepting a pending paste applies the scope. Cancel, Escape, and dismissal
discard pending bytes without changing preferences. The accepted bytes are
consumed before changing the policy, because disabling the viewport guard must
otherwise clear any pending paste.

Run-only suppression belongs to AppController memory and applies to all its
terminal windows. It does not rewrite settings and survives unrelated settings
saves. Restarting the application discards it. The durable confirmation setting
remains independent, so merely opening or applying settings cannot persist a
temporary choice. Turning durable confirmation back on clears suppression.

Never-ask reuses the existing schema-46 `confirmMultilinePaste` field. No schema
change is needed. Failed persistence keeps the previous preference and presents
contextual feedback in the owning window, without retrying the paste.

Shared action buttons use a contrasting inset keyboard focus ring in addition
to their outer focus border, including on accent-filled primary actions.
