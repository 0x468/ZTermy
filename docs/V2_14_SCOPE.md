# V2.14 scope: pre-V3 stable baseline

Status: implemented and release-gated for `0.2.14`

## Goal

V2.14 is the final `0.2.x` release. It does not add another broad product
surface. It turns the complete Windows 11 feature set delivered through V2.13
into a measurable daily-use baseline before the owner selects a V3 direction.

The release is complete only when persistence recovery, long-running terminal
responsiveness, repeated connection and teardown, network-worker ownership,
localization, Windows-native runtime behavior, and release artifacts are proven
together. Passing isolated unit tests is necessary but insufficient.

## Product changes

### Last-known-good persistence recovery

The following non-secret stores gain one bounded, atomic last-known-good backup:

- application settings;
- SSH profiles, excluding credentials;
- port-forwarding rules;
- scripts.

Workspace state already implements this contract and remains the reference
behavior. A save may back up only a primary document that the owning store can
fully validate. A malformed primary must never replace a valid backup. A
primary written by a newer unsupported schema must not fall back to an older
backup and must not be overwritten. Missing primary data still means an empty or
default store unless a valid backup exists.

Recovery is deterministic and observable through a non-secret warning and a
store recovery outcome. No path contents, profile fields, commands, credentials,
or file payloads are logged.

### Stability gate

A named V2.14 stability gate composes the existing focused tests instead of
duplicating them. Its automated contract includes:

- a configurable local-terminal soak with at least six latency windows;
- repeated local and SSH lifecycle checks with bounded shutdown time;
- SFTP cancellation, queue-generation, batch-recovery, and worker-stop checks;
- forwarding job ownership and restart checks;
- credential-vault concurrency, tamper, migration, and rollback checks;
- persistence corruption, backup recovery, future-schema refusal, and migration
  checks;
- session-log backpressure and restart checks;
- diagnostic privacy checks;
- the eight serial real-window gates.

The short developer gate uses a 30-second local soak. Release evidence also runs
the configurable 30-minute gate unless the owner explicitly records a shorter
accepted duration. The local input-queue P95 budget remains 16 ms per window,
shutdown remains below five seconds at application level, and no sustained
handle-growth regression is accepted.

### Diagnostic and recovery evidence

The release acceptance record contains the exact build, test, real-host, window,
translation, installer, artifact, and checksum evidence. Recovery messages name
only the logical store. Diagnostic export remains privacy-safe metadata and does
not absorb persistence payloads or command/session content.

## Compatibility and migration

- All valid `0.2.0` through `0.2.13` data stays readable through the migrations
  already owned by each store.
- The backup suffix is `.bak`; it is implementation state, not a user-editable
  second configuration surface.
- Unsupported future schemas fail closed. V2.14 never silently downgrades them.
- Installed and portable storage modes use the same non-secret recovery
  contract. Credential storage remains unchanged and separate.
- No stale legacy path is removed unless tests prove both migration and the
  safe failure boundary.

## Explicit exclusions

V2.14 does not add Linux/macOS support, AI, cloud sync, serial, Telnet, Mosh,
remote editing, or a new terminal protocol. It does not redesign the approved
UI, invent profile-level appearance, or add placeholder controls for V3.

## Release acceptance

The exact automated and retained human matrix is maintained in
`testing/V2_14_ACCEPTANCE.md`. V2.14 is tagged only after Debug and static Release
verification, focused real-host checks, Windows runtime evidence, and checksummed
ZIP/MSI artifacts are complete.
