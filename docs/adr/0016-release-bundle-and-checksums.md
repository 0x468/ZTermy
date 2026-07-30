# ADR 0016: Assemble a checksummed release bundle

- Status: Accepted
- Date: 2026-07-30

## Context

The static portable ZIP and per-user MSI were produced in separate build
directories. Manual acceptance therefore had to locate both files and copy
their hashes into an evidence record by hand. That is error-prone when several
locally rebuilt packages share the development version `0.1.0`.

V1 needs one authoritative handoff directory whose artifact names, version,
architecture, and digests can be checked before copying to another Windows 11
machine. The handoff must not depend on Git, network access, timestamps, or a
selected project license.

## Decision

The static build exposes `ztermy_release_bundle`. It depends on the validated
MSI and portable-package targets, then recreates one versioned release
directory containing exactly:

- the versioned portable ZIP;
- the versioned per-user MSI;
- `SHA256SUMS.txt` in the conventional two-space checksum format; and
- `release-manifest.json` with schema version, product version, platform,
  architecture, artifact kind, filename, and SHA-256 digest.

The assembly script rejects missing, empty, or unexpectedly named inputs,
refuses to delete outside the active build tree, and verifies the copied
digests. The static V1 preflight depends on this final bundle rather than on
the two distribution targets independently.

The manifest deliberately omits timestamps and source-control state so an
otherwise identical release description is deterministic. Code signing and
provenance attestations remain separate release-policy decisions.

## Consequences

- Manual and clean-machine testing begin from one authoritative directory.
- A stale MSI cannot be silently paired with a differently versioned portable
  archive.
- Hashes are generated from the exact files handed to the tester.
- The bundle does not alter or wrap either installable artifact and adds no
  license statement.
