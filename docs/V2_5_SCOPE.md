# V2.5 scope — daily-use lifecycle closure

Status: frozen for `0.2.5`

## Objective

Make the accepted V2.4 feature set safe to use as a daily terminal when remote
trees are large, windows and tabs close at awkward times, or the last process
was interrupted while saving state. V2.5 does not add a new visible workflow.

## Required contracts

### Bounded SFTP work

- At most 128 background tree-list commands may wait in one SFTP session.
- A duplicate tree request for the same path and navigation generation replaces
  the older queued request.
- A new root navigation invalidates queued tree work from older generations.
- File mutations run ahead of background tree discovery.
- Once stop begins, no new command is accepted and queued commands are removed.
  Callers receive a cancellation or bounded-capacity error rather than waiting
  forever.

### Recoverable workspace state

- The primary workspace file remains an atomic `QSaveFile` commit.
- Before replacing a valid primary file, its exact payload is atomically copied
  to a `.bak` last-known-good file.
- A damaged or unreadable primary file may recover from a valid backup.
- A file with a schema newer than this executable is never silently replaced or
  downgraded.
- The workspace and its backup contain layout/preferences only. Passwords,
  passphrases, private keys, and terminal input remain outside this store.

### Lifecycle evidence

- A real-window preflight repeatedly creates and closes local ConPTY tabs.
- The same gate leaves multiple sessions active and proves orderly application
  shutdown within a bounded time.
- Existing real-host SSH/SFTP smoke, non-real-host CTest, format, clang-tidy,
  QML quality, portable, MSI, and executable-contract gates remain required.

## Explicitly deferred

- remote resource monitoring and hover charts (V2.6);
- transfer pause/resume and Explorer drag-out;
- script triggers or a general automation runtime;
- session restoration beyond the current persisted workbench/preferences;
- new remote-platform support.

## Completion rule

Implementation is a V2.5 candidate only after all automated release gates and
static packaging pass. Daily-use acceptance additionally requires the manual
matrix in `testing/V2_5_ACCEPTANCE.md`; physical UI behavior is not inferred
from unit tests.
