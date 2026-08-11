# ADR 0057: Auditable AI usage and cost estimates

Status: accepted, 2026-08-12

## Context

The V3 assistant needs to expose provider token usage, first-token latency,
wall-clock latency, retries, and estimated monetary cost. Provider usage is
authoritative only for the fields the provider returns. Monetary cost is not:
models, service tiers, regional processing, long-context multipliers, cache
writes, modalities, and tool calls can all change the bill.

ztermy also allows a custom base URL. An endpoint speaking the OpenAI protocol
does not imply OpenAI pricing, even when it uses an OpenAI-looking model name.
Ollama and unknown local models may have useful token counts but no meaningful
per-token USD price.

## Decision

- `AiTurnRunner` measures one logical turn across bounded transport retries. It
  reports first visible text latency, total wall time, and completed retry count.
- Provider-reported token fields remain exact observations and are never
  synthesized from text length.
- `AiUsageEstimator` is a Qt-independent, tested catalog. A USD estimate appears
  only for the official HTTPS `api.openai.com` endpoint and a recognized model.
- The catalog records its review date (`2026-08-12`) and uses standard text-token
  rates from the official OpenAI model pages. The UI labels the result `Est.` and
  displays the catalog date.
- GPT-5.6 and GPT-5.4 requests above 272,000 input tokens apply the documented
  2x input and 1.5x output long-context multipliers and say so in the UI.
- Cached input is separated from uncached input. A model without a published
  cached-input rate conservatively prices any reported cached tokens at its
  normal input rate.
- Batch, Flex, Priority, regional, cache-write, image/audio, and tool-call fees
  are outside this first catalog. If any can affect a request, the displayed
  value remains explicitly an estimate rather than accounting data.

Official references reviewed for the initial catalog:

- <https://developers.openai.com/api/docs/models/gpt-5.6-sol>
- <https://developers.openai.com/api/docs/models/gpt-5.6-terra>
- <https://developers.openai.com/api/docs/models/gpt-5.6-luna>
- <https://developers.openai.com/api/docs/models/gpt-5.4>
- <https://openai.com/index/introducing-gpt-5-2/>

## Consequences

- Users can distinguish exact token observations from dated monetary estimates.
- Custom compatible providers and Ollama cannot accidentally inherit another
  vendor's price table.
- Model aliases and dated snapshots are supported only when they map
  unambiguously; unknown family variants intentionally show no USD value.
- Updating prices is a small, reviewable domain change with focused tests rather
  than a QML or provider-parser rewrite.
- Future service-tier and cache-write reporting requires additional request and
  usage metadata before it can be estimated honestly.

## Rejected alternatives

- **Always estimate from a model-name prefix:** misprices third-party gateways,
  mini/nano/pro variants, and future family members.
- **Fetch live prices at runtime:** introduces network availability, integrity,
  privacy, and silent semantic-change risks into the terminal UI.
- **Hide all cost information:** avoids stale prices but deprives a personal API
  user of an important budget signal.
- **Treat estimates as billing totals:** the provider invoice has information
  ztermy does not, so this would present false precision.
