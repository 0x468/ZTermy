# V2.11 scope: terminal workspace continuity

Status: approved implementation scope for `0.2.11`

V2.11 turns the existing tab/session model into a bounded native terminal
workspace. Each visible pane remains one custom terminal item backed by one C++
terminal session; terminal cells are never represented as QML object trees.

## Included

- horizontal and vertical binary splits, with at most eight panes in one
  workspace and at most 32 live sessions application-wide;
- active-pane focus, keyboard focus cycling, keyboard resizing, pointer divider
  resizing, close, duplicate, and move/swap operations;
- atomic persistence of split topology, ratios, active pane, tab order, and
  restore intents in a versioned workspace-state schema;
- fresh local-shell restoration and disconnected saved-profile SSH restoration;
- explicit reconnect for restored SSH intents, including portable-vault waiting,
  host-key prompts, jump/proxy profiles, and normal V2.9 reconnect behavior;
- compact/regular layouts, English/Chinese accessibility text, focus rings,
  mixed-DPI coverage, and deterministic minimum pane sizes.

## Restoration contract

- A local restore intent starts a new local shell. It does not claim that the
  previous process survived application exit.
- A saved SSH restore intent initially creates a disconnected pane with the
  profile identity and an explicit reconnect action. It never stores a secret
  and never opens the network merely because the application started.
- Unsaved quick connections are not persisted as reconnectable intents.
- Corrupt, unsupported, over-limit, cyclic, orphaned, or duplicate topology is
  rejected as a unit and the application falls back to the Hosts workspace.
- Structure is persisted only after a successful mutation; runtime screen,
  scrollback, selection, IME state, transient prompts, and remote processes are
  not serialized.

## Resource and performance boundaries

- Maximum eight leaves and fifteen total nodes per split tree.
- Split ratios remain within 20%–80%; each visible pane targets at least
  240x160 device-independent pixels before the UI offers another split.
- Snapshot delivery updates only the terminal items that display the changed
  session. Layout mutations never copy terminal cell grids.
- Resize requests are coalesced per pane and external I/O remains off the GUI
  and Qt Quick render threads.

## Deferred

- remote process resurrection, tmux/screen management, cloud-synced workspaces,
  cross-device handoff, pane broadcast input, collaborative sessions, and
  arbitrary startup command execution;
- detaching panes into native windows and moving them across processes;
- restoring unsaved passwords, private-key passphrases, or quick-connect
  endpoints.

## Exit gate

V2.11 is complete only when model/store/controller tests, runtime split/focus/
resize/close/restore evidence, local and real-host SSH checks, mixed-DPI UI
captures, static Release, CTest, static analysis, and packaged artifacts pass.
Manual cases that require subjective pointer, keyboard, or multi-monitor review
remain recorded for the final V2.14 owner pass.
