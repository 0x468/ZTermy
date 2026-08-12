# ADR 0075: privacy-safe AI diagnostics

Status: accepted

## Context

Users need to verify whether AI is active and what classes of data may leave
ztermy. A useful diagnostic export must not become a second transcript, leak a
provider endpoint or model identity, or expose MCP arguments and results. The UI
also needs a live, inspectable view rather than relying only on documentation.

## Decision

- Publish a read-only AI privacy diagnostic snapshot containing policy flags and
  bounded counts: provider kind, coarse endpoint scope, credential/model
  configured booleans, permission mode, automatic-context/history state, active
  conversations and requests, context item/redaction/truncation/byte counts,
  and MCP server/readiness/approved-tool counts.
- Classify an endpoint only as invalid, loopback, private network, or remote.
  Never include its scheme, host, port, path, query, or credentials in the
  snapshot. Never include the configured model identifier.
- State the provider-request boundary explicitly: user prompt, conversation
  messages, and previewed bounded evidence may be sent; raw terminal input,
  credential/private-key material, and arbitrary unselected files are excluded.
- State the diagnostic-export boundary explicitly: prompts, responses, terminal
  content, credentials, endpoint hosts, model identifiers, and MCP arguments or
  results are excluded.
- Show this snapshot in the AI Settings surface and embed the same metadata-only
  object in the existing privacy-safe diagnostic report. Refresh it from
  settings, credential, conversation, terminal-tab, and MCP lifecycle changes.

## Consequences

- The diagnostic report schema advances to version 2. It remains intentionally
  unsuitable for reproducing a conversation or provider request.
- Counts reveal local feature use but no content. Export remains an explicit
  user action.
- Provider-side retention and training policy remain controlled by the selected
  endpoint and account; ztermy can describe the local boundary but cannot claim
  remote deletion or confidentiality.

