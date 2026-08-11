# ADR 0058: Explicit AI command suggestions

- Status: Accepted
- Date: 2026-08-12
- Milestone: 0.3.0

## Context

The terminal assistant must support command generation without turning ordinary assistant prose into an execution
surface. Inserting text and running text are already separate native application actions. Provider-specific structured
output is not consistently available across OpenAI Responses, compatible chat endpoints, and Ollama.

## Decision

- Command generation is an explicit request mode. It adds an instruction that asks for exactly one runnable command in
  exactly one fenced block tagged for the active shell.
- ztermy exposes command actions only after the assistant message is complete.
- A suggestion is accepted only when the response contains exactly one supported shell code block, its trimmed content
  is non-empty, it contains no NUL, and it is at most 16 KiB.
- `Insert` uses the existing `insertTerminalCommand()` path and never appends Enter.
- `Run` uses the existing `runTerminalCommand()` path. Clicking it is the user's explicit authorization for that exact
  visible command, so no second generic warning is added.
- Normal prose and incomplete, ambiguous, oversized, or multiple command blocks never receive Insert or Run actions.
- Cancellation becomes visible immediately as a distinct `cancelling` state while the provider request is being torn
  down.

## Consequences

- The behavior is provider-neutral and works before the 0.3.1 write-tool permission model exists.
- The model can still fail to follow the command-format instruction; that failure is safe because no action appears.
- A future structured suggestion tool may replace the fenced-block transport without changing the native Insert/Run
  authorization boundary.

