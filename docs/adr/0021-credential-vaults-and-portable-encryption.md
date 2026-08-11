# ADR 0021: Credential vaults and portable encryption

## Status

Accepted

## Context

V1 kept SSH passwords and private-key passphrases in memory only. V1.2 needs
explicit credential persistence without placing secrets in profile JSON,
settings, logs, exported diagnostic report payloads, or command histories.
Crash minidumps can contain transient process memory and are therefore treated
as Secret artifacts, never attached or uploaded automatically; V3 adds bounded
secret lifetime, dump filtering, and synthetic-marker scans. Installed and portable
packages also have different user expectations: an installed application can
use the current operating-system account, while a portable directory must be
self-contained and usable on another computer.

Saving and authenticating are separate user actions. When a user explicitly
saves a password, ztermy must persist the submitted value even if it has not
yet been verified by a connection attempt.

## Decision

The application uses a platform-neutral `CredentialVault` boundary. A
credential is addressed by a stable opaque profile identifier and a kind
(`password` or `key-passphrase`). Profile documents may record only that opaque
reference; they never contain the secret.

Three backends are available:

- **System vault** uses Windows Credential Manager generic credentials and is
  the installed-mode default.
- **Portable encrypted vault** stores `credentials.zvlt` below the selected
  application data root and is the portable/custom-data default.
- **Session vault** keeps credentials in memory and clears them on shutdown.

The portable vault requires a master password of at least eight UTF-8 bytes.
It derives a 256-bit key with OpenSSL scrypt (`N=32768`, `r=8`, `p=1`) and uses
AES-256-GCM with a fresh random nonce for every atomic rewrite. The file header
records versioned algorithm identifiers and KDF parameters. The authenticated
payload uses a bounded binary format so plaintext buffers can be overwritten.
The master password and derived key are never serialized.

OpenSSL is reused because it is already a required, statically available
dependency for the SSH transport. The versioned envelope permits a future KDF
migration, including Argon2id, without inventing a new unversioned format.

Credential storage selection is global. Migration writes and verifies the
destination before changing the active backend. Source deletion is a separate
cleanup step so a cleanup failure can leave a duplicate but cannot lose the
only copy. Explicit batch clearing first snapshots every selected credential
and restores already removed entries if a later deletion fails. Profile
deletion removes its referenced credential by default.

An uninitialized portable vault is known to contain no credentials, so a user
may switch away from it without first creating a master password. Once the
vault file exists, it must be unlocked before its contents can be migrated.

## Consequences

- Saving a profile with a submitted credential persists it immediately; a
  later authentication failure does not silently delete it.
- Credential fields are cleared after submission. An explicit profile-edit
  action may read the secret from the active unlocked backend into a masked
  field so the owner can inspect or replace it; revealing it requires a
  separate accessible action, and closing the editor clears the UI copy.
  ztermy never logs or places the returned value in profile metadata.
- Saved status reflects keys available in the active backend rather than only
  profile metadata. Session-only references therefore become missing after a
  restart and prompt for a new credential instead of attempting a stale read.
- Copying a portable directory copies its encrypted credentials, but another
  computer still requires the master password.
- Losing the portable-vault master password makes its credentials
  unrecoverable. Profiles and non-secret settings remain usable.
- Directly deleting ztermy can leave system-vault credentials. The application
  therefore provides per-profile cleanup plus explicit clearing of active or
  inactive backend copies, and the installer may offer explicit user-data
  cleanup.
- Windows Credential Manager limits a generic credential blob to 2560 bytes;
  ztermy applies the same bound to every backend and never stores private-key
  file contents as a credential.
