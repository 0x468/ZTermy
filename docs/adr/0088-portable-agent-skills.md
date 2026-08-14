# ADR 0088: Portable Agent Skills with progressive disclosure

Status: Accepted

Date: 2026-08-15

## Context

Quick messages are editable prompt templates. Agent Skills are reusable instruction
packages whose metadata can be advertised to a model and whose full instructions
should be loaded only when relevant. Treating both as plain prompt snippets either
loses the Skill contract or permanently consumes model context.

The Agent Skills specification defines one directory per skill with a `SKILL.md`
file. Its YAML frontmatter carries a lowercase kebab-case name and description;
the Markdown body carries the instructions. NetCatty exposes user skills in its
slash picker, while OpenCode and current Agent Skills implementations use
progressive disclosure rather than injecting every body into every request.

## Decision

- Store user-managed skills under the active ztermy data directory at
  `Skills/<skill-id>/SKILL.md`, for both installed and portable modes.
- Scan only immediate child directories on a background worker. Reject symbolic
  links, non-regular entries, invalid UTF-8, oversized files, malformed
  frontmatter, name/directory mismatches, and unsupported control characters.
- Keep invalid entries visible as warnings so one broken package never hides
  valid skills.
- Advertise bounded name/description metadata through `list_skills`; load one
  exact body through `load_skill`. Never inject every skill body automatically.
- Let the user explicitly select up to four Skills from the composer's `/`
  picker. Explicit selections are frozen for that turn and injected as
  user-authored instructions; retry preserves the same selection.
- Keep selected Skills local to the active terminal's composer. Switching tabs
  clears transient chips, and Agent tools remain bound to the current terminal.
- Keep quick messages and Skills distinct in storage, settings, UI labels, and
  request construction.

Supporting files referenced from a Skill body are not readable in this slice.
A later extension must add a separately bounded resource tool instead of silently
expanding the scan surface.

## Consequences

- Provider requests stay compact when many Skills are installed.
- Installed and portable builds share one visible workflow without a Web or Node
  runtime.
- Skills remain portable folders that can be edited with any text editor.
- A user must reload after changing disk contents; the UI reports ready and
  warning counts without blocking the Agent pane.
- The catalog and tool contract require deterministic limits and dedicated tests.

