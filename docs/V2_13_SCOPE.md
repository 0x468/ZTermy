# V2.13 scope: scripts and local notes

Status: implemented and release-gated for `0.2.13`

V2.13 turns the existing command-snippet and structured-recording surfaces
into a durable script library and adds a local Markdown notes workspace. Both
features remain local-first, bounded, and independent from credentials.

## Script library

- Replace the presentation concept of a quick command with a versioned script
  definition containing a name, description, shell scope, typed variables, and
  one or more ordered command steps.
- Support non-secret variable types: text, integer, boolean, and choice. Names
  use identifier syntax and template references use `${name}`. Values are
  substituted verbatim and the fully rendered commands are always shown before
  execution; ztermy does not claim shell escaping on the user's behalf.
- Require an explicit running terminal target for every execution. The active
  terminal may be preselected, but the review surface shows its title and
  identity and the target is fixed when execution begins.
- Each step may continue immediately or wait for a case-sensitive literal
  output marker with a finite timeout. Matching is incremental across transport
  chunks and uses a bounded rolling byte window; regular expressions, prompt
  scraping, and unlimited capture are excluded.
- Allow one active script execution per terminal. Cancellation, target closure,
  disconnect, timeout, and application shutdown terminate the execution without
  sending later steps.
- Keep structured recording limited to commands issued through trusted ztermy
  command surfaces. A reviewed recording can be saved as a script; raw terminal
  keystrokes, passwords, and prompt replies are never recorded.
- Migrate existing quick-command documents without losing order, identifiers,
  timestamps, multiline command text, or shell scope. Import remains explicit
  and never overwrites an existing identifier silently.

## Script bounds

- At most 256 scripts, 32 variables per script, and 32 steps per script.
- At most 8 KiB per command step, 64 KiB of rendered commands per execution,
  1 KiB per literal output marker, and a 64 KiB rolling match window.
- Output timeouts range from 1 to 300 seconds. No unattended schedule, implicit
  reconnect execution, global broadcast, arbitrary code runtime, or secret
  variable type ships in V2.13.

## Local Markdown notes

- Store notes as ordinary UTF-8 `.md` files below a dedicated data-root
  directory. Folders map to directories so installed and portable modes remain
  inspectable, backup-friendly, and exportable.
- Provide folder/note create, rename, move, delete, Markdown editing, dirty-state
  protection, bounded search, and import/export. File writes are atomic.
- Expose the same notes workspace from the terminal workbench side panel. Notes
  are global rather than profile-specific; the selected note and workbench page
  may be restored as non-secret UI state.
- Reject absolute paths, traversal, reserved names, symlinks/reparse points that
  escape the root, non-Markdown files, malformed UTF-8, and oversized content.
- Bound the repository to 1,000 notes, 2 MiB per note, 32 MiB total indexed
  content, 16 folder levels, and 200 search results. Search must run away from
  the GUI/render threads and publish only the latest generation.

## Notes exclusions

- Notes are not encrypted and must not be presented as a credential store.
- V2.13 does not add cloud sync, collaboration, live preview with executable
  HTML/JavaScript, remote editing, filesystem watching outside the notes root,
  or automatic capture of terminal output.

## Completion gate

- Versioned migration, validation, rendering, trigger, cancellation, target,
  persistence, notes path-safety, atomic-write, search-generation, and shutdown
  tests pass in dynamic Debug and static Release.
- QML format/lint, translation completeness, keyboard navigation, light/dark,
  compact-width, mixed-DPI, and native lifecycle gates pass.
- Manual acceptance covers editing and importing scripts/notes, executing a
  rendered multi-step script on local and real SSH targets, timeout/cancel/tab
  close, Unicode Markdown, search cancellation, and restart recovery.
