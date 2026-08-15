# ADR 0090: Provider-native AI web search

Status: Accepted

Date: 2026-08-15

## Context

Current terminal work sometimes depends on changing information that is not in
the terminal or the model's training data. A generic ztermy-owned HTTP search
tool would require an additional search provider, credentials, ranking and page
extraction while losing the citation contracts already supplied by model
providers.

The supported protocols are not interchangeable. OpenAI Responses accepts the
`web_search` built-in tool and streams `response.web_search_call.*` plus output
text annotations. Anthropic Messages exposes versioned server-side web search
tools, streams `server_tool_use` / `web_search_tool_result`, and emits cited
source metadata. OpenAI-compatible Chat Completions and Ollama do not define a
portable equivalent.

References:

- <https://platform.openai.com/docs/quickstart#extend-the-model-with-tools>
- <https://platform.openai.com/docs/api-reference/responses-streaming/response/content_part>
- <https://platform.claude.com/docs/en/agents-and-tools/tool-use/web-search-tool>
- <https://platform.claude.com/docs/en/agents-and-tools/tool-use/tool-reference>

## Decision

- Web search is an explicit composer mode owned by the current terminal's AI
  sidebar. It never discovers or targets another terminal.
- Use provider-native search only:
  - OpenAI Responses receives `{ "type": "web_search" }`;
  - Anthropic Messages receives the GA basic
    `web_search_20250305` server tool with `max_uses: 5`;
  - compatible Chat Completions and Ollama expose the control as unavailable
    and reject an impossible request instead of silently ignoring it.
- Map provider wire events into provider-neutral typed events for search start,
  query, completion, and cited source. Search activity appears inline with the
  answer; citations remain structured data rather than Markdown appended by
  string manipulation.
- Deduplicate sources by URL and enrich an earlier URL-only source when a later
  citation supplies a title or excerpt. Accept only bounded HTTP(S) sources,
  count their bytes against the message budget, and persist them only on
  assistant messages in encrypted conversation history.
- Display a collapsed source section under the assistant answer. Each source is
  keyboard reachable and opens the original URL. Copying the answer continues
  to copy the provider's original Markdown, not ztermy's source-card rendering.
- The first implementation replays the accumulated assistant answer text on a
  later turn but does not persist opaque provider search-result payloads such
  as Anthropic `encrypted_content`. A later web-enabled prompt may therefore
  search again. Exact provider-native result replay and `pause_turn` continuation
  require a separate bounded wire-context contract before they are enabled.

## Consequences

- The common UI and conversation model remain provider neutral while each
  provider keeps its native search and citation behavior.
- Unsupported providers fail visibly; users are never told a model searched
  when its protocol could not do so.
- Source cards survive application restart without weakening the encrypted
  conversation-store boundary.
- Anthropic basic search is chosen for broad model compatibility. Newer dynamic
  filtering versions are not selected implicitly because their model and code
  execution requirements differ.
- Exact opaque search-result replay remains an explicit follow-up rather than an
  undocumented compatibility guess.
