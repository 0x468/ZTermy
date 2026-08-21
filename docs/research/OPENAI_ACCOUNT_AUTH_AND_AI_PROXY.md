# OpenAI account authentication and AI-only proxy research

Date: 2026-08-21

## OpenAI account sign-in

OpenAI currently documents ChatGPT-plan access for its Codex clients: the
Codex app, CLI, IDE extension, and web product. OpenAI also documents ChatGPT
and API Platform billing as separate systems. The public documentation does
not define a general third-party OAuth contract that lets an unrelated native
terminal exchange a ChatGPT subscription for ordinary model API access.

References:

- <https://help.openai.com/en/articles/11369540-using-codex-with-your-chatgpt-plan>
- <https://help.openai.com/en/articles/9039756-billing-settings-in-chatgpt-vs-platform>
- <https://learn.chatgpt.com/docs/auth>

Hermes Agent implements a different, product-specific path. Its public source
hard-codes the Codex OAuth client identifier, obtains ChatGPT tokens, and sends
inference traffic to `https://chatgpt.com/backend-api/codex`:

- <https://github.com/NousResearch/hermes-agent/blob/main/hermes_cli/auth.py>
- <https://github.com/NousResearch/hermes-agent/blob/main/website/docs/integrations/providers.md>

That proves technical feasibility but not a stable or supported third-party
provider contract. It also consumes Codex product allowance rather than making
a ChatGPT subscription behave like normal OpenAI API credit.

### Recommendation

Do not present the Hermes-style path as official OpenAI account login and do
not ship it as a default provider. Continue using the documented OpenAI API-key
path. Re-evaluate account sign-in when OpenAI publishes a third-party client
registration, OAuth scopes, inference endpoint, model-discovery contract, and
usage terms for this purpose.

Implementing the current Codex-specific private path would additionally require
an explicit owner decision to narrow ADR 0093: provider authentication would
need to be permitted while external Agent runtime integration remained banned.
Until both the product boundary and provider support contract are resolved,
account sign-in is a researched blocker rather than an implementation backlog
item.

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
