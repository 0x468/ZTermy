# 0125: SSH authentication negotiation and credential sources

## Status

Accepted

## Context

The host editor currently treats the authentication method and its secret as
one profile field. This is adequate for an explicit password, local key file,
or Windows SSH agent, but it cannot accurately represent OpenSSH certificates,
server-advertised authentication methods, or keyboard-interactive challenges.
Showing an "automatic" option only in QML would therefore be misleading.

The keychain already owns reusable password, private-key, certificate, and
agent identities. A profile may also deliberately reference a local private
key file without importing it. Authentication runs on worker threads and must
never block the GUI or log prompts, answers, passwords, or passphrases.

## Decision

- Keep credentials in the keychain or active credential vault. A host profile
  stores references and policy, never private-key material or plaintext
  answers.
- Present credential selection progressively: saved identity, local key file,
  password, or SSH agent. An empty local-key field shows the conventional
  `.ssh/id_ed25519` path only as a hint; it is not saved implicitly.
- Add automatic negotiation only together with transport support. It first
  reads the methods advertised by the server, then attempts only methods for
  which the selected credential source has usable material.
- A "prefer keyboard-interactive" policy moves keyboard-interactive ahead of
  password when the server advertises it. It does not intercept terminal input
  and does not turn arbitrary shell prompts into authentication prompts.
- Keyboard-interactive is a bounded request/response exchange between the SSH
  worker and the owning session UI. Each challenge carries a title,
  instruction, and at most eight prompts; answers are held in sensitive byte
  buffers and cleared after the attempt.
- The same negotiation contract is used by terminal, SFTP, jump-host, and
  transfer connections. Background operations that cannot display a challenge
  fail as "credential required" instead of guessing an answer or hanging.
- Explicit password, key, certificate, and agent modes remain available and
  attempt exactly the selected method.

## Consequences

The editor can expose automatic authentication only after the profile schema,
connection request, libssh2 adapter, and session prompt path land together.
Until then, the existing explicit modes remain truthful. Certificate
identities continue to resolve to their private key plus OpenSSH certificate
path through the keychain resolver.

This design does not scan arbitrary user files or silently try every key in
`.ssh`. Supporting an OpenSSH-agent/default-key discovery policy later requires
an explicit privacy and latency decision.
