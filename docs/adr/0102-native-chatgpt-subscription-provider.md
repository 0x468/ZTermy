# ADR 0102: Native ChatGPT subscription provider

- Status: Accepted
- Date: 2026-08-21

## Context

ztermy already supports API-key and local model providers for its one built-in
terminal assistant. Some users instead have a ChatGPT subscription whose Codex
allowance can be used through ChatGPT account authentication. OpenCode, Hermes
Agent, and Pi demonstrate a convergent implementation: OAuth authorization,
refreshable ChatGPT credentials, an account identifier, and a dedicated Codex
Responses transport distinct from the ordinary OpenAI API.

OpenAI now documents browser and device-code ChatGPT login, account state,
model discovery, and Codex rate-limit reporting through Codex App Server. App
Server is the supported integration boundary for products embedding the Codex
harness. ztermy deliberately does not embed that harness under ADR 0093; its
assistant, terminal tools, conversations, and permission model remain native.

References:

- <https://learn.chatgpt.com/docs/app-server>
- <https://learn.chatgpt.com/docs/auth>
- <https://github.com/anomalyco/opencode/blob/dev/packages/opencode/src/plugin/openai/codex.ts>
- <https://github.com/NousResearch/hermes-agent/blob/main/hermes_cli/auth.py>
- <https://github.com/earendil-works/pi/blob/main/packages/ai/README.md>

## Decision

Add a native provider presented as **OpenAI (ChatGPT subscription)**. It is an
authentication and inference adapter for ztermy's existing assistant, not a
Codex Agent integration.

The adapter will:

- offer browser PKCE login first and device-code login as a fallback;
- persist access, refresh, identity, account, expiry, and plan metadata through
  ztermy's active credential storage rather than application settings;
- refresh expiring credentials automatically and support explicit logout;
- use a dedicated Codex Responses transport and provider-specific model
  catalog rather than treating subscription tokens as OpenAI API keys;
- expose available model, account-plan, login-state, and Codex usage-limit
  information through ordinary provider settings and the assistant header;
- apply the global AI-only proxy to authorization, token refresh, model
  discovery, usage lookup, and inference;
- keep all network operations asynchronous and cancellable;
- preserve the same one-sidebar/one-terminal tool ownership, conversation
  store, permission modes, retry semantics, and UI used by every other native
  provider.

No Codex executable, App Server, CLI, SDK, RPC protocol, external thread,
external conversation store, Agent selector, or process lifecycle is added.

## Protocol boundary

The officially documented stable way to embed the complete Codex product is
App Server. Direct subscription inference used by OpenCode, Hermes, and Pi is
an ecosystem compatibility contract rather than a separately documented
public OpenAI API contract. Therefore the implementation must isolate OAuth,
endpoint construction, headers, model filtering, response replay, and usage
mapping behind one provider adapter. Protocol failures must produce a clear
provider-specific error and must never corrupt ordinary OpenAI API settings or
credentials.

The user-facing name intentionally says **ChatGPT subscription**, while source
types may use `OpenAiCodexSubscription` where protocol precision is required.
Subscription allowance is never described as OpenAI API credit.

## Settings and migration

Authentication mode belongs inside the OpenAI provider experience:

- **ChatGPT subscription**: Sign in, account status, model, reasoning, usage,
  logout.
- **API key**: Base URL, optional endpoint override, key, model, reasoning.

The two credential kinds use distinct stable references. Switching modes does
not delete the inactive credential without an explicit logout/remove action.
Schema changes are monotonic and include migration, round-trip, rejected-future
schema, and unrelated-setting preservation tests under ADR 0101.

## Consequences

- ChatGPT subscribers can use their Codex allowance without configuring API
  billing.
- ztermy remains one coherent native terminal assistant rather than becoming
  an external Agent host.
- The direct compatibility transport may change independently of the ordinary
  OpenAI API; focused contract fixtures and clear degradation are mandatory.
- Browser callbacks, device-code polling, refresh races, cancellation, proxy
  selection, credential replacement, logout, startup restoration, and model
  refresh require deterministic tests before the provider is exposed in UI.
