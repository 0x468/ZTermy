# ADR 0104: Completion-oriented AI command execution

Status: accepted, 2026-08-22

## Context

ADR 0056 separated command dispatch from lifecycle waiting: `run_command`
returned as soon as the terminal accepted the input, and the model then called
`wait_command`. That boundary is useful internally, but exposing it as the
ordinary model workflow causes needless tool-call churn and makes short
commands look asynchronous even though users expect an Agent to observe their
result before continuing.

The GUI and terminal I/O must remain responsive while a command runs. At the
same time, not every shell supplies verified command boundaries or exit status,
so a timeout or a quiet rendered frame must not be presented as confirmed
completion.

## Decision

- `run_command(command, timeout_ms)` is completion-oriented from the model's
  perspective. Its default deadline is 30 seconds and its maximum deadline is
  10 minutes.
- Dispatch and waiting remain asynchronous inside Qt. A timer observes command
  lifecycle or terminal-frame progress and resumes the provider turn without
  blocking the GUI, render, PTY, or SSH threads.
- With verified semantic lifecycle, `run_command` returns bounded output,
  coverage metadata, and exit status when the command finishes.
- Without verified lifecycle, a changed frame that remains quiet for 750 ms may
  finish the tool as `idle_unverified`. This is explicitly marked
  `completion_confirmed: false`; it is useful evidence, not a fabricated exit.
- On deadline expiry, `run_command` returns `status: timeout`, bounded partial
  output or frame evidence, and `completion_confirmed: false`. Timeout does not
  imply that the remote process stopped.
- `wait_command` remains available after a semantic command times out, and
  read tools remain available for additional bounded observation. The model
  should use them only when continuation is useful, not after every ordinary
  command.
- Background execution remains shell-native. A model may deliberately submit a
  command using the current shell's background syntax and later inspect it, but
  ztermy does not rewrite commands or invent detached-process semantics.
- Replay identity and at-most-once dispatch from ADR 0056 remain unchanged. A
  provider retry receives the cached dispatch/result evidence and never repeats
  a side effect.

This ADR supersedes only the ordinary `run_command` return timing described in
the Tool lifecycle section of ADR 0056. Its identity, ownership, interruption,
and uncertainty contracts still apply.

## Consequences

- Typical commands require one model tool call instead of a mechanical
  `run_command` plus `wait_command` pair.
- The user sees a responsive terminal and AI panel while the provider turn is
  paused for terminal evidence.
- Long-running commands return useful partial evidence at a bounded deadline;
  the Agent can explicitly wait or read again when needed.
- Degraded shells remain useful without pretending that a quiet frame proves
  process completion or a particular exit status.

## Rejected alternatives

- **Block the GUI thread until command completion:** violates the native UI and
  terminal responsiveness contract.
- **Always return immediately and require `wait_command`:** exposes internal
  orchestration overhead to every provider turn and encourages redundant tool
  calls.
- **Treat frame quietness as verified completion:** incorrect for commands that
  pause output, interactive programs, remote latency, and prompts.
- **Automatically append background syntax:** changes user/model intent and is
  not portable across shells.
