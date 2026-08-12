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
- the store is bounded to 50 conversations, 200 messages per conversation,
  128 KiB per message, 6 MiB plaintext, and 90 days by default; retention keeps
  the most recently updated conversations;
- non-persistent session credential storage cannot enable durable history;
- explicit deletion removes the primary and recovery envelope plus its data
  key; explicit export is the only operation that writes decrypted JSON.

Conversation replay remains untrusted evidence. Loading old text never restores
tool approval, agent control, target ownership, or a permission grant. Those are
reconstructed from live policy outside the transcript.

The storage implementation is synchronous at its infrastructure boundary. Its
application integration must call it through an owned serialized worker and
must not block the Qt Quick render or GUI thread.

## Consequences

- Copying the encrypted history file without its credential-vault key is not
  sufficient to decrypt it.
- Losing or deleting the key makes the encrypted history unavailable; ztermy
  does not silently generate a replacement over an existing file.
- A structurally valid but modified envelope reaches GCM authentication and is
  rejected rather than interpreted as partial history.
- Decrypted exports are intentionally sensitive user-owned artifacts and do not
  share the metadata-only audit export contract from ADR 0060.
- Rollback detection across an attacker replacing both the primary envelope and
  its recovery copy is outside the local non-adversarial-user threat model; the
  generation still prevents accidental cross-generation AAD reuse.
