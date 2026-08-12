# ADR 0081: Provider reasoning, Markdown, and deterministic turn cancellation

Status: accepted

## Context

Reasoning controls are not portable JSON. OpenAI Responses, Anthropic Messages,
DeepSeek, Kimi model generations, Z.AI, generic OpenAI-compatible servers, and
Ollama expose different fields and continuation requirements. Treating every
endpoint as `/chat/completions` with a shared `thinking` switch produces invalid
requests and can lose the signed reasoning state required by a tool loop.

The first assistant UI also displayed provider text as plain text and waited for
`QNetworkReply::abort()` to finish before ending the logical turn. That made
Markdown hard to scan and could leave Cancel or Retry looking stuck even though
the user had already requested cancellation.

## Decision

- Store a provider flavor separately from its wire protocol. Branded compatible
  providers remain OpenAI-compatible transports while retaining their own
  documented reasoning contract.
- Expose only adapter-known reasoning choices. `Automatic` means “do not
  override the provider/model default”; unknown compatible and Ollama models do
  not receive guessed reasoning fields.
- Map OpenAI Responses to `reasoning.effort`, current Anthropic models to
  adaptive thinking plus `output_config.effort`, DeepSeek/Kimi/Z.AI to their
  documented compatible fields, and reject an unsupported saved choice instead
  of silently inventing a mapping.
- Preserve provider-exposed reasoning text in its separate collapsed UI region.
  Do not claim access to hidden chain-of-thought. Anthropic thinking signatures
  and compatible `reasoning_content` are retained exactly for tool continuation.
- Render assistant source through Qt's native Markdown document support. User
  messages remain plain text, and Copy always uses the untouched conversation
  source rather than rendered rich text.
- Cancelling an active HTTP turn ends the local logical turn synchronously.
  Late network callbacks are ignored by request identity while the provider
  client remains responsible for reclaiming the aborted reply.
- The opt-in JSONL trace records exact bounded request/stream bytes and tool
  continuation data for debugging. Credentials remain in request headers and
  are never included in the trace payload.

## Consequences

- Provider/model capability tables and request-contract tests are required as
  APIs evolve; generic compatibility cannot promise branded features.
- A provider can expose a reasoning summary or reasoning content, but ztermy's
  UI must label it as model-provided and keep it visually subordinate to the
  final answer.
- Cancellation is responsive even when a network backend delays its finished
  signal. Retry starts from the last user prompt after the cancelled/failed
  assistant row has been excluded from provider history.
- Rich tool activity remains a native card concern rather than Markdown emitted
  by the model; card state and result data must come from the typed tool runtime.
