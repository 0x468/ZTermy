# ADR 0095: OpenCode ACP current-terminal bridge

- Status: accepted
- Date: 2026-08-15

## Context

ADR 0093 and ADR 0094 establish a bounded ACP v1 protocol, process, Session,
and event lifecycle. The application still needs to translate ACP terminal and
permission requests into ztermy's existing current-terminal Agent contract.
That translation must not let an external Agent select another tab, bypass the
existing permission modes, block the GUI thread, or confuse a remote shell
working directory with the local Agent process directory.

OpenCode is the first ACP target. It is discovered from `PATH`, owns its own
provider login/model configuration, and is launched locally with `opencode
acp`. Its terminal operations nevertheless act only on the terminal tab that
owns the AI sidebar.

## Decision

- Add `AcpAgentTurnRunner` as the application bridge for one ACP Client, one
  ACP Session, and one owning terminal. It exposes no Session-list or target
  selection API.
- Keep two explicit working directories: an existing local absolute directory
  for the Agent process, and the current terminal's absolute working directory
  for the ACP Session. This permits a local Windows Agent to control a remote
  POSIX terminal without treating the remote path as a local process path.
- Translate ACP `terminal/create` into the existing typed `run_command` path.
  Arguments, environment, and cwd are assembled with shell-specific quoting;
  `terminal/output`, `wait_for_exit`, `kill`, and `release` observe or control
  only the returned opaque handle for that command.
- Reuse ztermy's Read-only, Ask, Auto, and YOLO decisions. ACP
  `session/request_permission` appears in the existing approval surface;
  selected `allow_once`/`allow_always` decisions authorize the corresponding
  terminal creation without a second prompt.
- Poll command completion with a Qt timer rather than blocking the GUI thread.
  Running command snapshots include retained live output and explicit
  truncation; completed output and exit status retain the existing semantic
  command-block coverage contract.
- Cancellation completes pending ACP permissions, tool dispatch, and terminal
  waits before cancelling the Prompt. Shutdown stops both Agent discovery and
  every tab-owned runner.
- Add OpenCode to the Agent selector and Settings. External Agents use their
  own provider/model configuration, while terminal context, permission mode,
  conversation presentation, tool activity, and cancellation remain owned by
  ztermy.

## Consequences

- OpenCode can be selected without embedding Node or a Web runtime in ztermy.
- Focus changes cannot retarget an active turn; reconnect-generation checks
  invalidate stale callbacks rather than applying them to a new terminal.
- ACP usage is retained as context-window usage/cost and is not mislabeled as
  provider token accounting.
- Filesystem ACP capabilities remain disabled. Any future file integration
  needs a separate decision and cannot be inferred from this terminal bridge.

## Evidence

- `acp-agent-turn-runner`: discovery, terminal create/output/wait/release,
  shell-safe command construction, streamed text/thought/activity, async tool
  approval, and ACP permission completion.
- `app-controller`: discovered fake `opencode.exe`, current-terminal command
  dispatch, conversation completion, Agent selector state, and shutdown.
- `ai-command-tracker`: retained output and coverage are visible while a
  semantic command block is still running.
- <https://agentclientprotocol.com/protocol/v1/terminals>
- <https://agentclientprotocol.com/protocol/v1/tool-calls>
- <https://dev.opencode.ai/docs/acp/>
