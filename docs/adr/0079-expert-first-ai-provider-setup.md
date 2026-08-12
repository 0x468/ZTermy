# ADR 0079: AI provider setup is expert-first and protocol-backed

Status: accepted

## Context

ztermy is a personal terminal for technical users. The initial AI settings
exposed the internal credential reference and endpoint-path split as separate
product concepts. That mirrored implementation boundaries, added friction, and
did not match the direct provider setup used by mainstream model clients.

Provider brands and wire protocols are related but not identical. DeepSeek,
Kimi, Z.AI, and many self-hosted gateways use an OpenAI-compatible chat
protocol; OpenAI Responses, Anthropic Messages, and Ollama need dedicated
adapters. Users should select familiar providers without learning this internal
dispatch model.

## Decision

- Present one compact workflow: Provider, API address, API key, Model, and
  `Fetch models`. The API key is stored through the existing vault internally,
  but no credential-reference concept appears in the interface.
- Ship presets for OpenAI (ChatGPT/Codex models through the OpenAI API),
  Anthropic Claude, DeepSeek, Kimi, Z.AI GLM, and Ollama, plus a generic
  OpenAI-compatible option. Presets select default addresses and protocol
  adapters; they do not lock the address or model fields.
- Keep the model field editable. Model discovery is asynchronous, cancellable,
  bounded to a 2 MiB response, and only improves convenience; providers without
  a list endpoint still work with a manually entered model identifier.
- Resolve request URLs from the user-facing API address. A bare host receives
  the adapter's standard full path. A trailing slash or version suffix is an
  explicit prefix and receives only the endpoint leaf. A trailing `#` marks an
  exact generation endpoint and disables model discovery for that address.
  Always show the resolved generation URL below the form.
- Keep protocol JSON, authentication headers, model-list parsing, and streaming
  event mapping in C++ infrastructure adapters. QML only deals in provider
  tokens, addresses, keys, and model identifiers.
- Optimize defaults for direct expert use. Necessary secret storage, response
  limits, and cancellation remain transparent implementation details rather
  than additional setup steps.

## Consequences

- Adding a branded provider that already speaks a supported protocol usually
  requires only a preset and tests; adding a new protocol requires request,
  stream, and model-catalog adapters.
- Users can paste a gateway root address, inspect the resolved request, fetch a
  model list, or type a model directly without managing endpoint fragments or
  credential aliases.
- OpenAI API access remains API-key based. This decision does not claim that a
  ChatGPT consumer subscription or an unofficial account-login flow is an API
  credential.
- Exact endpoints marked with `#` trade automatic model discovery for explicit
  routing and remain an advanced compatibility escape hatch.
