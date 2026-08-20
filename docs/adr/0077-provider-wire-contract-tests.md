# ADR 0077: Provider wire contracts remain adapter-specific

Status: accepted

## Context

OpenAI Responses, Anthropic Messages, OpenAI-compatible chat completions, and Ollama expose similar
streaming text but use different request bodies, event envelopes, termination
signals, and error shapes. A provider-neutral UI is not evidence that all three
wire protocols actually work. Live-model evaluations are also non-deterministic
and cannot replace deterministic HTTP lifecycle tests.

## Decision

- Keep provider request construction and stream mapping behind the existing
  provider-neutral client boundary. Do not make the QML panel or conversation
  model branch on provider wire formats.
- Maintain deterministic local HTTP contract fixtures for all four supported
  adapters: OpenAI Responses SSE, Anthropic Messages SSE, OpenAI-compatible
  chat-completions SSE, and Ollama NDJSON.
- For each adapter, assert the outgoing path and body shape, incremental typed
  text events, usage normalization, final event, and one terminal completion.
  The shared client tests continue to cover cancellation, authentication,
  transient retry, `Retry-After`, TLS/network failures, malformed payloads, and
  bounded response limits.
- Normalize provider failures through one structured parser before application
  state sees them. HTTP error bodies and mid-stream error envelopes retain the
  provider message, canonical provider error code, HTTP status, request ID, and
  retry hint when supplied. The parser understands the native OpenAI Responses,
  Anthropic, OpenAI-compatible/OpenRouter, Gemini/Qwen-compatible, and Ollama
  shapes while preserving the HTTP classification as the fallback authority.
- Bound visible provider prose to 4 KiB and never display an HTML gateway error
  page as assistant content. The owning terminal sidebar presents the useful
  provider message plus a compact diagnostic line; it does not expose protocol
  branches or require a separate provider-error workflow.
- Treat these fixtures as protocol evidence only. Reference cloud/local model
  quality, latency, and task success remain separately versioned evaluation
  evidence and cannot rewrite the deterministic baseline.

## Consequences

- A change that accidentally sends a Responses body to a compatible or Ollama
  endpoint fails locally without credentials or network access.
- Provider differences remain isolated in C++ infrastructure rather than
  leaking into application state or presentation.
- Authentication, quota, rate-limit, context-size, invalid-request, network,
  and transient server failures now produce actionable current-terminal errors
  and stable retry decisions instead of a generic HTTP status message.
- Passing these tests does not claim that an external provider is available or
  that a particular model satisfies the V3 evaluation rubric.

## Protocol references

- [OpenAI API error codes](https://developers.openai.com/api/docs/guides/error-codes)
- [Anthropic API errors](https://platform.claude.com/docs/en/api/errors)
- [Gemini API troubleshooting](https://ai.google.dev/gemini-api/docs/troubleshooting)
- [OpenRouter errors and debugging](https://openrouter.ai/docs/api/reference/errors-and-debugging)
- [Alibaba Cloud Model Studio error codes](https://www.alibabacloud.com/help/en/model-studio/error-code)
