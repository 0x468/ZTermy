# V3 AI product review — explicit context and conversation-first UX

Date: 2026-08-13

## Verdict

The previous panel exposed implementation concepts as primary navigation. The
underlying capabilities were useful, but their hierarchy was not competitive
with terminal-native assistants. The corrected product model is:

1. a conversation is the primary object;
2. ordinary terminal activity is not implicit model context;
3. the user explicitly attaches a selection or recent semantic command blocks;
4. evidence produced by Agent tool calls is retained with the conversation;
5. low-frequency diagnostics and contextual actions live behind overflow;
6. New conversation and History remain first-class, discoverable actions.

This is a product correction, not a reduction in Agent capability.

## Evidence from reference products

### Warp

Warp documents terminal blocks and selections as context the user attaches to
an Agent conversation. Commands run inside the Agent conversation are then part
of that conversation, and attached context remains available for follow-up
turns. Warp also separates conversations so unrelated work can start with a new
conversation.

References:

- <https://docs.warp.dev/agent-platform/local-agents/agent-context/blocks-as-context>
- <https://docs.warp.dev/agent-platform/local-agents/agent-context>
- <https://docs.warp.dev/agent-platform/local-agents/interacting-with-agents>

### NetCatty

The locally inspected GPL-3.0 reference tree at `D:\tmp\Netcatty` uses a
conversation header with Agent selection, export, history, and new-chat
actions. Tool calls appear inline, reasoning is collapsible, and a selected
terminal region can be added to the conversation. Its Markdown renderer gives
wide code and table content local overflow instead of widening the whole
assistant panel.

NetCatty is a product-behavior reference only. No source, artwork, styling, or
branding is copied into ztermy.

### Codex CLI and Claude Code

Both products treat the Agent session/conversation as the durable follow-up
context and provide explicit continue/resume workflows. They are useful
references for agent lifecycle and permissions, but Warp and NetCatty are the
closer references for SSH-terminal context attachment and panel placement.

References:

- <https://learn.chatgpt.com/docs/codex/cli>
- <https://docs.anthropic.com/en/docs/claude-code/cli-usage>

## Corrected ztermy contract

### Default request context

A normal question sends the accumulated AI conversation only. It does not
silently scrape the terminal viewport, scrollback, current command, or recent
manually executed commands.

### Explicit terminal attachment

The composer attachment menu supports:

- current terminal selection;
- last completed command;
- last 3 completed commands;
- last 5 completed commands.

These are semantic command blocks, not arbitrary viewport guesses. The selected
items apply to the next request. After that request, the exact bounded evidence
is retained as part of the conversation so follow-up questions remain coherent.
When a shell does not expose semantic command boundaries, the same actions
degrade to an explicitly labelled approximate attachment containing the recent
locally captured commands and current terminal frame. They remain usable rather
than failing merely because OSC 133/633 is unavailable.

### Agent-generated evidence

Every tool invocation performed by the Agent contributes its typed arguments
and bounded result to hidden conversation evidence. Later turns receive that
evidence even when the visible terminal has moved. Ordinary user terminal I/O
does not enter the conversation unless explicitly attached.

### Conversation UX

- History and New conversation are compact header actions.
- Durable encrypted history is enabled by default when the selected credential
  vault can persist its key; users may turn it off. Session-only or locked
  portable storage degrades explicitly instead of presenting a dead history
  button.
- Clear is expressed as New conversation, not as destructive-looking utility
  chrome.
- Explain last failed command is a contextual shortcut under overflow, not a
  permanent primary button.
- Activity is a diagnostic view under overflow. Tool progress remains visible
  inline where it is relevant.
- A restored stored conversation restores visible user/assistant messages plus
  bounded evidence, but never revives permissions, pending operations, or PTY
  ownership.

### Markdown layout

Normal prose wraps within the assistant pane. Fenced code and Markdown tables
are isolated blocks with their own horizontal scrolling. Neither block may
increase the assistant pane's width or overlap the terminal.

Each fenced code block exposes a compact top-right Copy action that copies only
the original code body. Streaming updates are coalesced before Markdown layout
work reaches the Qt Quick thread.

### Conversation scrolling and reasoning

The conversation follows streaming output only while the user remains at the
bottom. Scrolling upward suspends following and reveals a Return to latest
action; returning to the bottom resumes following. Provider-exposed reasoning
opens while it is the active stream, collapses when the answer begins, and
remains manually expandable afterward.

### NetCatty parity audit

The reference was re-inspected in both its running Windows application and its
local source tree on 2026-08-14. Parity is tracked by user-facing capability,
not by copying its React/Electron component structure.

| Capability | ztermy state | Decision |
| --- | --- | --- |
| Dedicated terminal-side Agent surface | Implemented | Native QML workbench; resizable and movable |
| New conversation, encrypted history, restore | Implemented | First-class compact actions |
| Conversation export | Implemented | Export the current raw user/assistant Markdown as `.md` |
| Model and permission controls beside the composer | Implemented | Model and five professional execution modes share the footer |
| Selection and recent terminal attachments | Implemented | Prefer semantic blocks; explicitly label plain-shell fallback as approximate |
| Streaming, cancel/retry, inline tools, reasoning | Implemented | Sticky-to-bottom only while the reader remains at the bottom |
| Markdown tables/code and per-code copy | Implemented | Native renderer with local horizontal overflow |
| Provider presets, model discovery, reasoning controls | Implemented | OpenAI Responses, Anthropic, DeepSeek, Kimi, Z.AI, Ollama, compatible endpoints |
| Reusable permission rules and Agent modes | Implemented | More granular than NetCatty's observer/confirm/auto baseline |
| MCP tools | Implemented | Native typed registry and lifecycle rather than UI scripting |
| Built-in slash commands | Implemented | Keyboard-first picker for conversation, history, context, explanation, and command-generation actions |
| Reusable user-authored skills | Planned | `0.3.10`; extend the same picker without coupling skills to one provider |
| Explicit local text/file and image attachments | Implemented | `0.3.10`; never ambient or silently attached |
| Optional web search | Implemented | Provider-native OpenAI Responses/Anthropic search with typed activity and persisted citations; unsupported protocols degrade explicitly |
| External Agent/SDK selector | Permanently rejected | Superseded by ADR 0093; Codex, OpenCode, Claude Code, and other external Agent/harness runtimes must never enter the roadmap or implementation |

The current composer follows the reference hierarchy: identity and conversation
lifecycle at the top; message field first; attachment, model, execution mode,
and send controls in one footer. At the minimum 320 px workbench width those
controls compact rather than overflowing into the terminal.

### Restored SSH tabs

A restored SSH tab intentionally does not reconnect without an explicit user
action. Its disconnected state must expose Reconnect both in the terminal state
card and in the tab context menu.

## Rejected alternatives

- Always attach the current terminal frame: rejected because unrelated manual
  activity pollutes prompts and makes context unpredictable.
- Hide conversation history behind a diagnostics view: rejected because resume
  and new-conversation lifecycle are primary Agent UX.
- Wrap code and tables as ordinary prose: rejected because it destroys command
  formatting and table readability.
- Keep a permanent row of Explain/Attach/Activity/History/Clear buttons:
  rejected because it gives rare actions more visual weight than the
  conversation itself.

## Acceptance focus

- A plain question has zero terminal evidence unless it came from earlier Agent
  tools or an explicit attachment.
- One explicit attachment is visible in request context, then remains usable in
  a follow-up without re-scraping the terminal.
- An Agent-run command and its result remain available to later turns.
- Long unbroken code and wide tables scroll locally at narrow panel widths.
- Code blocks copy their source without Markdown fences.
- Streaming follows only at the bottom and never pulls an upward-scrolled
  reader away from older content.
- Plain SSH sessions can attach recent approximate terminal activity without
  requiring shell integration.
- History count and preview ignore hidden evidence rows.
- A restored SSH tab makes Reconnect obvious and actionable.
