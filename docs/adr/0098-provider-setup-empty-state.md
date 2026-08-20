# ADR 0098: Provider setup is an assistant empty state

- Status: Accepted
- Date: 2026-08-21

## Context

The built-in terminal assistant previously exposed its normal composer before
its provider was usable. A missing API address or model was detected only after
Send, while an unavailable credential failed after the turn had already begun.
That made the first successful conversation depend on an avoidable error.

Current SSH-terminal products treat this as onboarding rather than a failed AI
turn. CtrlOps documents an `AI Not Configured` panel state that links directly
to provider setup, discovers models after credentials are entered, and keeps
manual model entry available. Wave and Tempest likewise keep model context and
provider choice in the assistant workflow instead of requiring a separate
external Agent runtime.

References:

- <https://ctrlops.io/docs/modules/ai-terminal>
- <https://docs.waveterm.dev/waveai>
- <https://gotempest.app/ai-terminal>

## Decision

The terminal assistant derives one reactive readiness condition from the
existing application settings:

- the API base address is non-empty;
- the model is non-empty; and
- a credential exists for network providers, while Ollama remains keyless.

When that condition is false, the owning terminal sidebar displays a themed,
keyboard-accessible setup card and one primary **Open AI settings** action. The
action opens the existing Settings tab directly on its AI category. The normal
composer is hidden until the condition becomes true and returns automatically
after settings are saved. Existing conversation history remains readable; the
state does not create, clear, or retarget a conversation.

Provider fields and credentials remain owned by the existing Settings surface.
The sidebar does not duplicate a setup form, add a wizard, automatically send a
network request, or expose credential-vault terminology. Model discovery and
editable fallback remain the connection-oriented validation path in Settings.

This state belongs only to the terminal that owns the sidebar. It introduces no
cross-terminal routing and no external Agent discovery, selector, process, or
protocol integration.

## Consequences

- A new user reaches the actionable configuration in one click instead of
  learning through an error.
- URL, model, credential, and Ollama behavior have one visible readiness rule.
- The unavailable composer cannot accumulate misleading draft state or emit a
  preventable failed turn.
- Real-window smoke tests must verify the card, accessibility semantics, direct
  AI-settings navigation, configured transition, and compact geometry.

