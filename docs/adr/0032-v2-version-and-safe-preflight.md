# ADR 0032: V2 version identity and safe release preflight

Status: accepted

## Context

The accepted V1 product milestone shipped from project version `0.1.0`. The
V1.1 through V1.9 names described product-development milestones rather than
Semantic Versioning releases. Reaching the next stable personal-workspace
contract needs a distinct Windows executable, MSI, portable archive, and
release-manifest identity without claiming a mature public API or a `2.0.0`
compatibility boundary.

The existing automated preflight also invoked CTest without excluding the
opt-in `ssh-real-host` test. That test normally skips without its environment,
but a release command must not become network-active merely because it inherits
developer variables.

## Decision

- V2 starts at project version `0.2.0` and every V2 candidate or maintenance
  release remains in the `0.2.x` line. V3 is the first milestone allowed to
  use `0.3.0`. `project(VERSION)` remains the single source for PE metadata,
  runtime version text, MSI identity, archive names, and manifests.
- A release codename and its verse belong to the `x.y` product line. Patch
  releases keep that identity unchanged; the owner chooses a new codename and
  verse only when the minor component changes. Therefore every `0.2.z` release
  uses the V2 codename `此` and its approved verse.
- Historical V1 evidence retains its `0.1.0` paths and hashes.
- The release preflight excludes `ssh-real-host` by exact test name. Real-host
  password, private-key, SFTP, and transfer checks remain separately invoked,
  explicitly authorized evidence.
- `ztermy_v2_automated_preflight` is the documented V2 entry point. The old V1
  target remains as a compatible implementation dependency for existing local
  workflows.

## Consequences

- Installing the V2 MSI exercises the established stable upgrade GUID while
  clearly distinguishing the product version from V1.
- A normal CI or developer release command cannot contact a real SSH host even
  if opt-in environment variables are present.
- The `0.2.x` line communicates ongoing V2 personal-project evolution; it does
  not promise a public SDK or cross-platform compatibility contract.
- Candidate repairs increment the patch component only when a new immutable
  artifact is cut; development commits do not opportunistically claim `0.3.0`.
- Patch maintenance does not create artificial branding churn or require a new
  release-identity card for every repair build.
