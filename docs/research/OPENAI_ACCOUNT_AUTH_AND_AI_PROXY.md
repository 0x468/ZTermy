# OpenAI account authentication and AI-only proxy research

Date: 2026-08-21

## OpenAI account sign-in

OpenAI documents ChatGPT-plan access for Codex and now explicitly documents
Codex App Server as the integration surface for rich clients embedded in other
products. Its account protocol supports managed browser login, managed
device-code login, account/plan inspection, logout, token refresh, model
discovery, and Codex usage-limit reporting. OpenAI also documents ChatGPT and
API Platform billing as separate systems: a ChatGPT subscription does not
become ordinary OpenAI API credit.

References:

- <https://help.openai.com/en/articles/11369540-using-codex-with-your-chatgpt-plan>
- <https://help.openai.com/en/articles/9039756-billing-settings-in-chatgpt-vs-platform>
- <https://learn.chatgpt.com/docs/auth>
- <https://learn.chatgpt.com/docs/app-server>

OpenCode, Hermes Agent, and Pi also expose ChatGPT subscription authentication
as a provider distinct from the ordinary OpenAI API. Their public
implementations use refreshable OAuth credentials, the ChatGPT account id, and
a dedicated Codex Responses transport:

- <https://github.com/anomalyco/opencode/blob/dev/packages/opencode/src/plugin/openai/codex.ts>
- <https://github.com/NousResearch/hermes-agent/blob/main/hermes_cli/auth.py>
- <https://github.com/NousResearch/hermes-agent/blob/main/website/docs/integrations/providers.md>
- <https://github.com/earendil-works/pi/blob/main/packages/ai/README.md>

This establishes both feasibility and a mainstream ecosystem pattern. The
direct inference endpoint used by those projects is not separately documented
as a stable public OpenAI API, however. The officially maintained embedding
surface is App Server, which includes the complete Codex harness and therefore
does not fit ztermy's permanent native-assistant boundary.

### Recommendation

Implement **OpenAI (ChatGPT subscription)** as a native ztermy provider under
ADR 0102. Browser PKCE is the primary desktop flow and device code is the
fallback. Keep OAuth, refresh, account metadata, model filtering, usage limits,
headers, and Codex Responses replay isolated behind one compatibility adapter.
Do not launch or bridge App Server or any other Agent runtime.

The product must describe this as Codex allowance included with the user's
ChatGPT plan, never as OpenAI API balance. If the compatibility contract stops
working, display a provider-specific actionable error while leaving the API-key
provider and its credentials untouched.

## AI-only proxy

Qt supports proxy selection on an individual `QNetworkAccessManager`, including
an object-specific proxy factory. ztermy already separates model requests and
model-catalog discovery from SSH/SFTP networking, so proxying only AI traffic
does not require an application-global proxy.

References:

- <https://doc.qt.io/qt-6.8/qnetworkaccessmanager.html>
- <https://doc.qt.io/qt-6/qnetworkproxyfactory.html>
- <https://doc.qt.io/qt-6/qnetworkproxy.html>

### Proposed product contract

Add one global **AI network proxy** setting with three mainstream modes:

1. **Use system proxy** (default): query the Windows system proxy for each AI
   destination through a manager-local proxy factory.
2. **Direct connection**: apply `QNetworkProxy::NoProxy` to both AI managers.
3. **Custom proxy**: accept an `http://`, `socks5://`, or `socks5h://` URL and
   apply the parsed proxy to both AI managers.

The setting governs provider chat/streaming requests, model discovery, and any
future provider authentication request. It never changes SSH, SFTP, port
forwarding, terminal telemetry, update checks, or other application traffic.
Optional proxy credentials should use the existing credential store and remain
an ordinary part of the proxy form rather than introducing a separate vault
workflow.

The persisted fields require application-settings schema 21. Following ADR
0101, the same change must include a schema-20 migration fixture and exact
round-trip tests. Runtime tests should cover direct bypass, custom HTTP and
SOCKS selection, system-factory selection, model discovery, streaming, proxy
authentication, and cancellation without blocking the GUI thread.
