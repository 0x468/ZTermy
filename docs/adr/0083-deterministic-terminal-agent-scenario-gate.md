# ADR 0083: Deterministic terminal-Agent scenario gate

Status: Accepted

## Context

The permission policy, action dispatcher, command tracker, semantic observer,
ConPTY session, and `wait_command` serializer each had focused tests. Those
tests did not prove that a mode decision could travel through the complete
local terminal path and return the finished command's retained output. They
also did not exercise user input queued while an Agent command was running or
an interactive prompt answered through `write_to_pty`.

Provider calls are deliberately excluded from this contract. Provider wire
adapters and live-model quality have separate deterministic and evaluation
gates; adding a network/model dependency would make the terminal execution
contract non-deterministic.

## Decision

- Add `ai-agent-scenario`, a provider-independent executable CTest that uses a
  real `LocalTerminalSession`, ConPTY, PowerShell, ephemeral shell integration,
  `SemanticTerminalObserver`, action dispatcher, command tracker, and
  `AiWaitCommandTool`.
- Exercise Read-only, Ask, Edit, Auto, and YOLO. Ask/Edit must stop at approval;
  Auto/YOLO execute; Read-only produces a typed denial without consuming a
  write action.
- Require a finished semantic command block, exact exit status, complete
  retained output, and cached at-most-once replay for executed commands.
- Queue direct user input while a long Agent command runs and require ordered
  completion of both commands without a visible ownership handoff.
- Answer a real PowerShell `Read-Host` prompt through `write_to_pty` and require
  the interactive command to finish with the supplied value.
- Cover a saved-SSH-Profile SFTP mutation matrix without network dependency;
  the existing real-host gates remain responsible for general transport and
  shell environment evidence.
- Add a separate environment-gated `ai-agent-real-host` contract. It sends
  automatic Agent commands through a real `SshTerminalSession`, queues direct
  user input behind a long Agent command, and answers a remote interactive
  `read` through `write_to_pty`. Offline test runs skip this case explicitly;
  a release candidate records a real key-authenticated execution.
- Include this scenario and the action-dispatcher contract in the mixed AI/MCP
  concurrency soak. Bump the content-free soak schema so an older report cannot
  be mistaken for evidence covering the expanded set.

## Consequences

The release gate now detects integration regressions that isolated policy,
terminal, or serializer tests cannot see. The deterministic local scenario adds
roughly one to two seconds to one mixed-soak iteration and requires
PowerShell/ConPTY, which is appropriate for ztermy's Windows 11 release
platform. The real-host scenario is not placed in the repeated soak because
network availability is an independent environment variable. A fresh formal
duration report is required for the final V3 candidate because the approved
local soak set changed.
