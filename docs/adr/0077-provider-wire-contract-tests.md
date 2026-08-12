# ADR 0077: Provider wire contracts remain adapter-specific

Status: accepted

## Context

OpenAI Responses, OpenAI-compatible chat completions, and Ollama expose similar
streaming text but use different request bodies, event envelopes, termination
signals, and error shapes. A provider-neutral UI is not evidence that all three
wire protocols actually work. Live-model evaluations are also non-deterministic
and cannot replace deterministic HTTP lifecycle tests.

## Decision

- Keep provider request construction and stream mapping behind the existing
  provider-neutral client boundary. Do not make the QML panel or conversation
  model branch on provider wire formats.
- Maintain deterministic local HTTP contract fixtures for all three supported
  adapters: OpenAI Responses SSE, OpenAI-compatible chat-completions SSE, and
  Ollama NDJSON.
- For each adapter, assert the outgoing path and body shape, incremental typed
  text events, usage normalization, final event, and one terminal completion.
  The shared client tests continue to cover cancellation, authentication,
  transient retry, `Retry-After`, TLS/network failures, malformed payloads, and
  bounded response limits.
- Treat these fixtures as protocol evidence only. Reference cloud/local model
  quality, latency, and task success remain separately versioned evaluation
  evidence and cannot rewrite the deterministic baseline.

## Consequences

- A change that accidentally sends a Responses body to a compatible or Ollama
  endpoint fails locally without credentials or network access.
- Provider differences remain isolated in C++ infrastructure rather than
  leaking into application state or presentation.
- Passing these tests does not claim that an external provider is available or
  that a particular model satisfies the V3 evaluation rubric.

