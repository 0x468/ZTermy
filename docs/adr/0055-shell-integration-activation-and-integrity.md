# ADR 0055: Shell-integration activation and integrity

Status: accepted, 2026-08-11

## Context

Reliable command text, boundaries, exit status, and working directory require
shell cooperation. Windows Terminal documents OSC 133 lifecycle marks and VS
Code extends this family with OSC 633 exact-command/nonced events. Microsoft
Intelligent Terminal demonstrates a practical installer that appends snippets to
PowerShell profiles and bash `.bashrc`.

ztermy cannot simultaneously promise universal remote rich integration and
promise never to touch shell startup state. It can, however, keep ephemeral
activation as the default and make persistent modification a separate,
reversible product operation. This ADR governs shell startup integration only;
it does not prohibit ordinary task-authorized access to other dotfiles such as
`.env`, `.gitignore`, or application configuration.

## Decision

Support three explicit states per shell/session:

1. **Ephemeral** (default): launch a ztermy-owned, versioned wrapper/init script
   for that session where the shell permits it. The wrapper may source the normal
   user startup path, but it does not edit it. Generated files live in a bounded
   application/session location and are removed on orderly close; stale files are
   safe to clean later.
2. **Persistent installed** (optional): only an explicit user action may install
   a guarded integration block. The UI previews the exact target and diff before
   applying it. Installation creates a timestamped backup, writes atomically,
   records shell/path/version/original hash/installed hash, verifies a new shell,
   and provides upgrade, uninstall, and restore.
3. **Disabled/unavailable**: no injection. Capability visibly degrades to basic
   or none; AI features cannot claim exact command/failure evidence.

Additional rules:

- Local PowerShell 7 is the first rich target; Windows PowerShell 5.1 is tested
  separately. ztermy never changes PowerShell execution policy automatically.
- Remote persistence is separately authorized per saved host and user identity.
  Quick connections cannot persist it. Remote files use a ztermy version marker
  and the same preview/backup/atomic/uninstall contract.
- bash, zsh, fish, PowerShell, nested SSH, and login/non-login startup paths have
  separate adapters and fixtures. Unsupported combinations degrade explicitly.
- `tmux`, shell-framework, and prompt-theme configuration is never rewritten
  automatically. Passthrough requirements are diagnosed and shown to the user.
- OSC 133 is interoperable navigation/lifecycle evidence. Exact command and
  privileged rich claims require a ztermy-owned event carrying an unpredictable
  per-session nonce. Unverified OSC data is untrusted.
- Installer rollback failure leaves the backup and a recovery instruction. It
  never repeatedly appends a second block or guesses how to merge a conflicting
  user edit.

## Consequences

- `0.3.0` can ship useful local PowerShell rich semantics without silently
  changing personal configuration.
- Persistent integration remains possible for users who value seamless future
  sessions, including saved SSH hosts, but it is an auditable operation.
- Some remote/nested shells remain basic or none until their adapter and test
  matrix are ready; this is a truthful capability state, not a defect hidden by
  heuristics.
- Installer records and backups are operational metadata, not credentials, and
  must have bounded retention and an explicit cleanup path.

## Rejected alternatives

- **Always append on first run:** low friction, but silently mutates local/remote
  user state and makes rollback/conflict handling unsafe.
- **Never modify any startup state:** prevents durable rich integration for many
  real shells and contradicts the product goal.
- **Treat raw input reconstruction as exact:** fails for history expansion,
  multiline editors, pasted input, shell rewriting, nested shells, and spoofed
  output.
- **Change execution policy or tmux configuration automatically:** widens a
  system/user trust boundary unrelated to one terminal session.
