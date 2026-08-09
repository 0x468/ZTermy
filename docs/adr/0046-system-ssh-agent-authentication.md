# ADR 0046: Authenticate through the local SSH agent

Status: Accepted

## Context

An SSH client should be able to use keys already unlocked by the operating
system without reading, copying, or persisting their private material. Treating
agent authentication as a private-key-file variant would incorrectly expose
file and passphrase fields and could leave obsolete credentials in ztermy's
vault when a profile changes method.

The pinned libssh2 Windows backend supports the agent protocol through Pageant
and the Windows OpenSSH agent named pipe. The public libssh2 API selects the
available backend; it does not expose a reliable backend selector.

## Decision

- `Agent` is a distinct authentication method in the domain, persistence,
  connection request, bootstrap, and UI layers.
- Agent profiles contain no private-key path, passphrase flag, credential
  reference, or connection secret. Switching an existing profile to Agent
  removes its previously managed credential transactionally.
- Authentication enumerates identities exposed by libssh2 and stops at the
  first accepted identity. The complete attempt shares the existing bounded
  authentication deadline and remains cancellable while network I/O is
  pending.
- Missing agents and empty identity lists report `AuthenticationUnavailable`;
  identities rejected by the server report `AuthenticationRejected`.
- The product calls this method **SSH agent**. Windows-specific help mentions
  Windows SSH Agent without claiming that another compatible agent can never
  be selected by libssh2.
- Remote agent forwarding is out of scope. Local authentication does not imply
  forwarding the agent into the remote session.

## Consequences

ztermy never handles the agent's private-key bytes or passphrases. Agent
availability and unlocked identities remain external operating-system state,
so the UI must explain failures without trying to start services or load keys.
Pageant compatibility is retained by the pinned dependency even though Windows
OpenSSH is the primary supported workflow.

## Verification

Unit tests cover Agent profile/request invariants, JSON round trips, UI-facing
profile maps, and removal of an old saved password when a profile changes to
Agent. An opt-in real-host gate verifies the explicit unavailable-agent status.
A successful direct real-host gate is opt-in through
`ZTERMY_TEST_SSH_AGENT_INTERACTIVE=1`; it must be run after the Windows agent is
enabled and contains a key accepted by the fixture host.
