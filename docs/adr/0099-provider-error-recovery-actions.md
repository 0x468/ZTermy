# ADR 0099: Provider failures expose typed recovery actions

- Status: Accepted
- Date: 2026-08-21

## Context

The built-in terminal assistant already classified provider failures as network,
authentication, rate-limit, quota, invalid-request, server, cancellation,
protocol, and context-overflow errors. The application flattened that result to
one string, however, so every failed response exposed the same Retry action and
duplicated the error in both a global status banner and the failed assistant
message.

Current terminal AI products keep configuration failures actionable and local
to the failed request. CtrlOps routes an unconfigured or unauthorized assistant
to its AI settings and documents separate authentication, quota, model-loading,
and connectivity recovery paths. Warp likewise treats an invalid or expired API
key as a non-retryable configuration condition rather than an undifferentiated
transport failure.

References:

- <https://ctrlops.io/docs/modules/ai-terminal>
- <https://ctrlops.io/docs/troubleshooting/ai-not-responding>
- <https://docs.warp.dev/reference/api-and-sdk/troubleshooting/errors/authentication-required>

## Decision

The provider error code remains typed through `AppController` and maps to a
small recovery plan:

- network, server, and cancellation failures offer **Retry**;
- authentication, rate-limit, quota, invalid-request, and protocol failures
  offer **AI settings** and **Retry**;
- context overflow offers **New conversation** instead of repeating the same
  oversized request;
- an unclassified internal failure retains a conservative Retry action and a
  concise settings hint.

The latest failed or cancelled assistant message owns the provider error,
recovery hint, and actions. While that inline message exists, the global error
status is suppressed so the same failure is never rendered twice. Actions use
the existing conversation retry, clear-conversation, and Settings-to-AI routes;
they add no modal wizard or provider-specific workflow. Clearing a conversation
also clears the old assistant-message anchor.

Recovery remains non-blocking, keyboard accessible, and contained by the
owning terminal sidebar at compact width. It never enumerates or targets
another terminal. This decision applies only to ztermy's built-in
provider-backed assistant and introduces no external Agent runtime, selector,
process, protocol bridge, or compatibility layer.

## Consequences

- Users receive the shortest relevant next action instead of interpreting a
  generic failure and retry loop.
- Authentication and configuration failures can be repaired without losing the
  failed prompt; Retry remains available after returning from Settings.
- Context-overflow recovery cannot accidentally resend the same oversized
  conversation from the inline action.
- Provider error parsing, recovery policy, controller exposure, duplicate
  suppression, accessibility, Settings navigation, and compact geometry require
  deterministic automated coverage.
