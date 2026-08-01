# ADR 0019: Successful-connection recency and Quick Connect

Status: superseded in part by ADR 0021

## Context

The V1.1 Hosts dashboard needs a direct endpoint workflow and a small
recent-connections region. A recent item must represent a connection that
actually authenticated, not merely a profile that was opened or a failed
attempt. Quick Connect must remain useful without turning transient
credentials into persisted profile data.

The existing SSH profile schema already stores non-secret connection metadata.
At the time of this decision, passwords and private-key passphrases were
ephemeral. ADR 0021 replaces that constraint when the user explicitly saves a
credential.

## Decision

- Quick Connect accepts `user@host`, `user@host:port`, and
  `user@[IPv6]:port`. Ports default to 22 and must be in the range 1–65535.
- Authentication is selected in a compact modal after the target is parsed.
  Private-key paths may be retained only as ordinary profile metadata.
  Passwords and private-key passphrases are cleared from QML immediately when
  a connection request is submitted and are never persisted or logged.
- Quick Connect does not save a profile by default. The user may explicitly
  opt into saving one before connecting.
- Recent connections contain saved profiles only. An optional
  `lastConnectedUtcMs` field extends the existing profile JSON schema without
  changing its version or invalidating older files.
- A profile's timestamp is updated only when its SSH session enters the
  authenticated `Connected` phase. Opening a profile, starting a connection,
  host-key review, cancellation, and failed authentication do not update it.
- The dashboard shows at most six recent profiles, ordered newest first.
  Editing a profile preserves its timestamp; duplicating a profile clears it.
- The recent list exposes only existing non-secret profile metadata and the
  connection timestamp.

## Consequences

- Failed or abandoned connections cannot pollute the recent list.
- Unsaved Quick Connect sessions leave no reusable profile or recent metadata.
- Users who explicitly save a Quick Connect target keep that profile even if
  its first connection attempt fails; it enters Recent only after a later
  successful authentication.
- Profile persistence remains backward compatible, while validation rejects
  malformed negative timestamps.
- Automated tests must cover target parsing, persistence compatibility,
  recency ordering, edit/duplicate behavior, keyboard focus restoration, and
  compact dialog reachability. Real-host acceptance must confirm success-only
  recency because authentication phase changes are asynchronous.
