# ADR 0072: Versioned AI evaluation replay

Status: accepted

## Context

Provider output is nondeterministic, while target integrity, evidence coverage,
approval, tool allowlists, and duplicate side effects must be deterministic
release gates. A checklist without a machine-readable corpus cannot distinguish
an implementation regression from ordinary model variance.

## Decision

- Store the twelve V3 terminal tasks in a versioned, provider-independent JSON
  corpus. Each case declares required evidence, allowed tools, and tools that
  require explicit approval.
- Replay bounded trace documents through `AiEvaluationHarness`. Reject unknown
  tools, inexact targets, missing approvals, missing evidence, and duplicate or
  unkeyed side effects.
- Cap documents at 2 MiB and corpora at 64 cases; reject malformed, duplicate,
  or unsupported schema identifiers.
- Label synthetic replay separately from live-provider evaluation. Synthetic
  success validates the contract, not model quality.
- Keep provider/model/token/latency/cost run records as a later result layer over
  the same stable case identifiers. Do not add task-specific model routing until
  repeated live evaluations demonstrate a measurable benefit.

## Consequences

- Core policy regressions are reproducible without network access or API keys.
- Cloud and local-model quality can be compared without weakening deterministic
  safety gates.
- The corpus schema becomes a release artifact and must be migrated explicitly
  when its contract changes.
