# ADR 0097: Bind sent context to the originating user message

- Status: Accepted
- Date: 2026-08-21

## Context

ztermy lets the user explicitly attach a terminal selection, recent command
blocks, text files, images, or a bounded terminal frame before sending a
message. Draft chips made these sources visible before sending, while the
serialized text evidence was retained only as a hidden provider message after
the turn started. Once the draft cleared, the visible conversation no longer
showed which evidence belonged to the user's question.

The previous transcript insertion order also began the assistant message before
persisting the request evidence. That placed the evidence after the assistant
message instead of beside the user message which introduced it. Follow-up turns
still received the data, but its semantic owner was wrong.

Warp associates terminal blocks with the conversation turn that consumes them,
and Wave stages terminal or file context before send. Local NetCatty product
inspection likewise shows sent attachments on the corresponding user message.
These are product-behaviour references only; ztermy keeps its own native Qt/C++
implementation and current-terminal ownership.

## Decision

1. Every sent user message owns a bounded list of context attachment summaries.
   A summary contains only its display title, kind, semantic evidence quality,
   and redacted/truncated flags. It never duplicates the attached content.
2. The exact bounded and redacted request evidence remains a hidden transcript
   entry, anchored immediately after the user message which introduced it.
   Assistant tool evidence remains anchored to the assistant message that ran
   the tool.
3. The visible conversation renders compact, wrapping context chips on the user
   message. Warning state and evidence quality are available without exposing
   raw hidden evidence or widening the terminal sidebar.
4. Encrypted conversation history stores the summaries and restores them with
   the user message. The optional additive field remains readable by existing
   schema-1 stores; absent data means no summaries.
5. Inputs are validated independently at model and storage boundaries. A user
   message retains at most 16 summaries; titles are limited to 1,024 UTF-8
   bytes and kind/quality tokens to 64 bytes. Invalid records cannot poison a
   stored conversation.
6. The feature is strictly scoped to the sidebar's owning terminal. It neither
   enumerates nor references another terminal and introduces no external Agent
   runtime.

## Consequences

- A user can tell what evidence an earlier question relied on after sending,
  restoring history, or continuing the conversation.
- Provider replay order now matches the intended evidence relationship:
  user question, attached evidence, assistant answer.
- The UI retains only lightweight metadata; potentially large terminal or file
  contents remain in the existing bounded hidden evidence channel.
- Old conversations remain valid and simply show no sent-context chips.

## References

- Warp, *Blocks as Context*:
  <https://docs.warp.dev/agent-platform/local-agents/agent-context/blocks-as-context>
- Wave, *Wave AI*:
  <https://docs.waveterm.dev/waveai>

