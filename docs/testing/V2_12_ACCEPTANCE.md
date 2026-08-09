# V2.12 acceptance: recursive and batch SFTP

This matrix covers the bounded recursive and multi-selection SFTP job system
introduced in `0.2.12`. A batch is a durable parent operation; regular files
continue to use the proven V2.8 byte-copy executor.

## Automated evidence

- Domain and planner tests cover iterative local/remote traversal, empty
  directories, deterministic parent-before-child ordering, path validation,
  duplicate destinations, depth/entry/root limits, cancellation, and the
  no-symlink-follow boundary.
- Materializer, executor, manager, and coordinator tests cover bounded child
  concurrency, aggregate progress/speed/ETA, batch pause/resume/cancel/retry,
  safe apply-to-remaining conflict policy, partial-file handling, and parent
  completion only after all children reach terminal states.
- Recovery-store tests cover atomic versioned persistence, size/count limits,
  active-batch-only serialization, interrupted-state normalization, malformed
  input rejection, and the explicit exclusion of credentials and live handles.
- Directory-model and controller tests cover keyboard and pointer range/toggle
  selection, parent-entry exclusion, multi-file/folder enqueue, batch controls,
  conflict routing, recovery publication, and orderly shutdown.
- QML compilation, `qmllint`, `qmlformat`, icon contracts, and English/Chinese
  translation completeness cover the virtualized transfer-center and SFTP UI.
- The opt-in real-host gate pins the expected SHA-256 host key, recursively
  uploads and downloads a unique tree through SFTP, verifies Unicode paths,
  file bytes, and an empty directory, then removes the remote fixture.

## Manual acceptance retained

1. In SFTP list and tree views, select discontiguous rows with Ctrl, ranges with
   Shift, additive ranges with Ctrl+Shift, and all rows with Ctrl+A. The `..`
   parent row must never become selected and the selected count must stay exact.
2. Upload one folder containing nested Chinese names, empty directories, and
   several files. Repeat by dropping multiple files and folders. The parent
   card must show aggregate progress, speed, ETA, and expandable child details.
3. Download multiple files and a directory to a chosen local folder. Verify
   file bytes, empty directories, local-path copy/reveal, and completed-file
   drag-out behavior in Explorer.
4. Exercise Ask, Replace, Rename, and Skip conflicts. “Apply to remaining” must
   persist only Skip or Replace for queued siblings; Rename must still request
   a unique destination and Cancel must never become sticky.
5. Pause, resume, cancel, retry, and dismiss a batch and an individual child
   during discovery and transfer. No click may be lost to snapshot refresh, no
   completed child may restart, and partial files must retain the resume/cleanup
   contract.
6. Exit during discovery, upload, download, pause, and conflict attention.
   Restart must show interrupted active batches without reconnecting or moving
   bytes until explicitly resumed; no secret may appear in the recovery file.
7. Try a symlink/reparse-point loop, an inaccessible directory, an over-depth
   tree, and invalid/reserved Windows paths. Each must fail or skip explicitly
   without escaping the selected root or deleting source data.
8. Inspect empty, discovery, progress, conflict, partial failure, interrupted,
   and completion states in light/dark themes, English/Chinese, keyboard-only,
   compact/regular widths, and 100%, 125%, 150%, and 200% DPI.
9. Close a terminal tab or ztermy while discovery and multiple transfers are
   active. Every planner, SFTP session, and worker must stop and join without a
   crash, assertion, late prompt, leaked handle, or retained process.

## Release evidence

- MSVC dynamic Debug: configured and built successfully; 56/56 CTest cases
  passed in 44.72 seconds.
- MSVC static Release: configured and built successfully; 56/56 CTest cases
  passed in 49.95 seconds.
- Formatting, all project C++ clang-tidy translation units, `qmllint`, and
  `qmlformat` passed. The QML format gate covered 43 files and the translation
  completeness gate covered 1084/1084 messages.
- The eight native-window runtime gates passed, including 100%, 125%, 150%,
  and 200% DPI, appearance, resize, layout, keyboard, terminal rendering, and
  lifecycle coverage.
- The opt-in recursive SFTP round trip passed against the pinned real host in
  both dynamic Debug and static Release builds.
- Release bundle:
  `build/msvc-static-release/package/release/ztermy-0.2.12-windows-x64`.
  It contains exactly the portable ZIP, MSI, SHA-256 list, and JSON manifest.
- Portable ZIP SHA-256:
  `6ef5d0e1f072a03435afad5f7e1ac007e017e584b702688e1e455d8395880aca`.
- MSI SHA-256:
  `ee5f1cd0e32fb7734d2637d9cb4e8cd20f987ba91629abe03b36498091ec8a72`.
- Retained environment limitation: the WiX ICE01-ICE105 contract-validation
  step could not access the disabled Windows Installer service and exited with
  WIX0217/217. The service was intentionally left unchanged. MSI generation,
  bundle structure, artifact names, sizes, hashes, and manifests succeeded;
  this evidence does not claim that ICE validation passed.
