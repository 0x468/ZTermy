# ADR 0061: encrypted AI conversation store

- Status: Accepted
- Date: 2026-08-12

## Context

AI conversations can contain commands, terminal output, paths, host details, and
provider responses. Session-only retention remains the safe default, but an
explicitly enabled history needs durable installed and portable storage without
placing large transcript bodies in Windows Credential Manager or the portable
credential vault.

The credential-storage preference may migrate between the system and portable
backends. A history format must survive that migration, fail closed when its key
or schema is unavailable, and never downgrade to plaintext.

## Decision

ztermy stores optional conversation history in a dedicated encrypted envelope:

- one random 256-bit data-encryption key is created per history store;
- the key is stored as the dedicated `AiConversationKey` credential kind under
  the existing credential-vault coordinator, so normal vault migration moves
  only the small key;
- transcript bodies remain in a separate last-known-good file and never enter a
  credential record, application settings, workspace state, diagnostic logs, or
  crash annotations;
- each rewrite uses AES-256-GCM with a fresh 96-bit nonce and 128-bit tag;
- schema version and a monotonically increasing rewrite generation are bound as
  additional authenticated data;
- envelope and plaintext schemas are independently validated and unsupported
  versions fail closed;
- completed assistant turns retain their bounded presentation snapshot: visible
  Markdown, provider-exposed reasoning, native tool activity cards including
  bounded argument/result snapshots, citations,
  usage, latency/retry metrics, cost metadata, truncation state, and opaque
  provider replay needed for an exact follow-up;
- the store is bounded to 50 conversations, 200 messages per conversation,
  128 KiB per message, 6 MiB plaintext, and 90 days by default; retention keeps
  the most recently updated conversations;
- non-persistent session credential storage cannot enable durable history;
- explicit deletion removes the primary and recovery envelope plus its data
  key; explicit export is the only operation that writes decrypted JSON.

Conversation replay remains untrusted evidence. Restored tool activities are
historical presentation records, not executable calls. Their arguments and
results exist only so the owner can inspect what happened; they cannot be
redispatched from history. Loading old text never
restores tool approval, agent control, target ownership, a pending action, or a
permission grant. Those are reconstructed from live policy outside the
transcript.

The storage implementation is synchronous at its infrastructure boundary. The
application owns a single-thread serialized history model that performs every
load, rewrite, export, and delete away from the Qt Quick render and GUI thread.
Only immutable results return to the GUI model.

Retention is opt-in. Disabling retention stops future transcript writes but
does not silently delete existing ciphertext; the user can still export or
delete that history explicitly. Locking the active portable vault immediately
forgets decrypted in-memory history. Restoring a transcript creates no grants,
budgets, pending actions, or tool ownership, and the same stored conversation
cannot be restored into two live terminal tabs at once.

## Consequences

- Copying the encrypted history file without its credential-vault key is not
  sufficient to decrypt it.
- Losing or deleting the key makes the encrypted history unavailable; ztermy
  does not silently generate a replacement over an existing file.
- A structurally valid but modified envelope reaches GCM authentication and is
  rejected rather than interpreted as partial history.
- Decrypted exports are intentionally sensitive user-owned artifacts and do not
  share the metadata-only audit export contract from ADR 0060.
- A portable history cannot be enabled until its vault is initialized and
  unlocked. Startup may expose an already-enabled history as unavailable until
  the user unlocks that vault; successful unlock reloads it asynchronously.
- Selecting session-only credential storage disables future history retention.
  Source cleanup is rejected while an encrypted history envelope exists, so a
  generic credential migration cannot implicitly destroy its only durable key;
  history deletion remains an explicit operation.
- Rollback detection across an attacker replacing both the primary envelope and
  its recovery copy is outside the local non-adversarial-user threat model; the
  generation still prevents accidental cross-generation AAD reuse.
