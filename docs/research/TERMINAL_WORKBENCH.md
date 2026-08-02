# Terminal workbench research

Status: product and architecture baseline for V1.4, 2026-08-02

## NetCatty reference audit

The installed NetCatty application and the clean local source tree at
`D:\tmp\Netcatty` were inspected as a behavioral reference. No source or asset
is incorporated into ztermy.

Observed terminal behavior:

- A terminal-local toolbar sits above the viewport and exposes identity,
  telemetry, search, scripts, and other terminal actions. In the live SSH
  window observed on 2026-08-02, the left side showed
  `root@192.168.1.25:22`, Copy address, Timestamp, System, CPU, Memory, Disk,
  and Network. The right side showed Highlighting, SFTP, Composer, Find,
  Session log, Scripts, and an overflow menu.
- Quick control opens a panel beside the viewport rather than overlaying it.
  Its default position is the left side and its default width is approximately
  half of the available terminal surface. The terminal reflows immediately.
  The panel is per tab, may live on either side, persists its width, and is
  clamped to a usable range.
- The panel has a horizontal icon tab strip for File transfer, Scripts,
  History, Theme, System, Notes, and AI, followed by move/split and close
  controls. History provides search, refresh, current host/global scopes, an
  entry count, and per-row Run, Paste, and Save-as-snippet actions. These row
  actions are independently keyboard-accessible rather than being hidden
  behind a single ambiguous row click.
- Scripts provides Library and Running views, search, create-snippet, new
  script, and start-recording entry points. The pinned snippets shown by the
  Composer are sourced from this library. V1.4 code snippets intentionally
  implement only the named-command subset of that larger script model.
- System opens the same reflowing side panel. It is a substantial feature with
  Overview, Processes, tmux, and Docker pages; the overview includes CPU,
  memory, disk, network, load, uptime, OS/kernel, swap, SSH latency, per-core,
  mount, interface, and process data. This evidence rules out a superficial
  V1.4 telemetry placeholder.
- The bottom Composer is independently toggleable and vertically resizable.
  It reflows the terminal, shows pinned snippet chips above its editor, exposes
  snippet management, and sends the editor contents with Enter while
  Shift+Enter inserts a newline. Pinned snippets insert on click and send on
  Shift+click, with a full-command tooltip. It can coexist
  with an open side panel and the terminal Find bar; all three regions reflow
  the viewport without covering terminal cells. ztermy adopts this direct,
  keyboard-first interaction.
- Find opens a compact row directly below the per-terminal toolbar, with match
  navigation on the right. It does not replace or close the workbench or
  Composer.
- The overflow menu contains History, Configuration directory tracking,
  Terminal settings, Recording, and Terminal encoding. Only actions with a
  production backend are shown in ztermy; V1.4 does not ship inert replicas.
- Opening and resizing the panel reduces the terminal viewport and therefore
  changes terminal rows/columns. Hidden panel contents are retained selectively
  to avoid expensive remounts.

Relevant NetCatty source responsibilities:

- `components/terminalLayer/TerminalLayerSidePanelSection.tsx` owns panel side,
  width, resizing, icon routing, and close/move actions.
- `components/HistorySidePanel.tsx` owns scopes, filtering, virtualization, and
  per-entry actions.
- `application/state/useRemoteHistoryState.ts` owns request identity, pending
  retries, per-session cache, and stale-result suppression.
- `electron/bridges/sshBridge/sessionOps.cjs` opens a second channel on an
  authenticated SSH connection, detects the account shell, and reads bounded
  Bash/Zsh/Fish history files.
- `domain/remoteHistory.ts` parses shell-specific formats, orders by timestamp,
  removes duplicate commands, and filters product-generated entries.
- `components/terminal/runtime/terminalCommandExecution.ts` attempts to infer
  submitted commands from xterm state for a product-local global history.

The last technique is adopted only in a deliberately conservative form for the
open-session global scope. ztermy records a submission only while the input
stream remains unambiguous and drops a pending capture after navigation or
editing control sequences. It does not inspect terminal cells and does not
persist the resulting global view. Full-screen applications, multiline
editors, nested shells, IME composition, and password prompts remain reasons
to prefer future semantic shell integration.

## Comparable product patterns

- Windows Terminal treats the terminal as a host rather than a shell. Its
  command palette routes named application actions. Its newer recent-command
  suggestions rely on semantic shell integration using OSC 133 prompt,
  command-start, command-executed, and command-finished marks rather than blind
  input capture. This is the correct long-term model for ztermy command blocks,
  but it requires shell cooperation and is deferred from V1.4.
- Tabby keeps quick command transmission in a plugin and exposes configurable
  shortcuts and plugin boundaries. This supports separating ztermy's command
  domain/store from its terminal and panel implementations.
- Termius treats snippets as explicitly saved reusable commands and supports
  deliberate execution, including multi-host use. ztermy V1.4 adopts explicit
  save and execution, but not cloud/team or multi-host behavior.
- Electerm separates quick commands and quick input from host bookmarks and can
  synchronize them independently. ztermy likewise keeps code snippets global
  and outside SSH profiles and the credential vault.
- Warp distinguishes ephemeral per-session history from reusable workflows.
  Its workflows are named, searchable, parameterizable, and optionally scoped;
  its command history can carry richer execution metadata. ztermy adopts the
  history/code-snippet separation now and leaves parameters and semantic
  metadata for later milestones.

Primary references:

- [Windows Terminal command palette](https://learn.microsoft.com/en-us/windows/terminal/command-palette)
- [Windows Terminal shell integration](https://learn.microsoft.com/en-us/windows/terminal/tutorials/shell-integration)
- [Tabby repository and plugin catalogue](https://github.com/Eugeny/tabby)
- [Termius snippets](https://termius.com/blog/snippets-sharing-is-on)
- [Electerm repository](https://github.com/electerm/electerm)
- [Warp command history](https://docs.warp.dev/terminal/entry/command-history)
- [Warp YAML workflows](https://docs.warp.dev/terminal/entry/yaml-workflows)

## Shell history constraints

History files are useful but are not a live semantic protocol:

- Bash reads `$HISTFILE` on startup and may not save the current session until
  exit unless the user's configuration appends incrementally. Timestamp marker
  lines delimit multiline entries.
- Zsh `EXTENDED_HISTORY` prefixes commands with start time and duration; its
  incremental append behavior is controlled by separate options.
- Fish normally stores history under `$XDG_DATA_HOME/fish`, supports named and
  private histories, and can avoid persistence entirely.
- PSReadLine exposes a configurable `HistorySavePath` and can save after each
  command, at exit, or not at all. V1.4 reads the documented default path for
  the ztermy local ConsoleHost and reports custom/disabled cases as unsupported
  instead of injecting commands into the interactive shell.

Primary references:

- [Bash history facilities](https://www.gnu.org/s/bash/manual/html_node/Bash-History-Facilities.html)
- [Zsh history options](https://zsh.sourceforge.io/Doc/Release/Options.html)
- [Fish history command](https://fishshell.com/docs/current/cmds/history.html)
- [PSReadLine history options](https://learn.microsoft.com/en-us/powershell/module/PSReadLine/set-psreadlineoption)

## Chosen product boundary

History is session data; code snippets are intentional user data. Remote
history is never automatically persisted or merged into a global database.
The global history scope aggregates bounded data from currently open tabs.
Code snippets are versioned global configuration and may be migrated into a
richer Scripts model later. Run sends directly to the active PTY.

## V1.4 toolbar disposition

Implemented as real V1.4 actions:

- active terminal identity and Copy address for SSH tabs;
- History, Scripts, Composer, and Find;
- Insert, Run, and Save-as-code-snippet actions;
- a compact overflow containing only available actions, including global
  Terminal settings.

Architecturally reserved but hidden until their own milestones:

- SFTP/file transfer, remote System/CPU/Memory/Disk/Network telemetry, session
  logs, timestamps, highlighting, notes, recording, configuration-directory
  tracking, and alternate encodings;
- full script automation beyond named code snippets, and AI assistance.

Not adopted:

- per-tab Theme settings, because ztermy intentionally uses one global terminal
  appearance policy;
- serial tooling and third-party branding or assets.
