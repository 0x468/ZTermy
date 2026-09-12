# Terminal interaction repair — 2026-09-12

Status: initial implementation and automated regression recorded below; subsequent
owner findings remain open and are tracked in `V5_EXPERIENCE_CONVERGENCE.md`.
Native terminal interaction takes priority over auxiliary UI.
No commit/push or production-data mutation is part of this verification run.

## 本轮交付摘要

- 已删除应用内 Ghost text、抢占普通按键的补全和默认持久命令索引。
  历史面板只合并本次会话命令与只读 Shell 历史文件，按时间排序、去重，
  不向交互终端发送 `history`，也不自动执行选中的历史项。
- 关闭终端的后台线程回收改为异步，并修复连续关页时容器回调重入、同步
  I/O 取消竞态；原始终端输入、选择、Shell 自带补全保持优先。
- SFTP/传输改为解析钥匙串身份，独立 SFTP 页面不再强行打开终端侧栏。
- 窗格统一为复制和新建（选择主机/Shell），默认向右；标题占用真实布局空间，
  拖动用于重排现有窗格，放大/标题状态按工作区隔离，脱离窗口独立管理。
- 补齐窄顶栏、标签下拉状态/关闭、双击设置、钥匙串空状态、日志列对齐、
  工作台侧栏宽度记忆、按数据目录隔离的可配置单实例，以及重启确认。
- Debug 和静态 Release 最终全量 CTest 均为 126/126，通过全量静态分析及
  后续改动的增量复查；QML、格式、翻译、资产、结构门禁通过。
  已提供真实窗口布局、键盘、材质、缩放、连续关闭及窗格渲染证据。

静态单文件：`build/msvc-static-release/ztermy.exe`（50,401,792 字节，0.4.4）。
SHA-256：`4D67AF78C97E79C73D18D6390EC5D8773CB4E57C07EDD5E8934ADE153E12D442`。
未制作 MSI，未提交、推送或发布。真实服务器认证及不同 GPU/Shell 配置按下面清单验收。

## Delivery checklist

- [x] R1: asynchronous transport shutdown, reentrancy-safe tab/pane close; continuous-close regression.
- [x] R2: keychain-aware SFTP and transfer authentication, independent browser visibility.
- [x] R3: remove inline completion and default persistent command index; read Shell history
  files on demand, merge exact duplicates with in-memory submitted session commands.
  Never inject `history` or history-flush commands into a terminal.
- [x] R4: Copy pane and New pane (profile/Shell chooser), default right split;
  reorder existing panes without creating sessions; workspace-owned zoom/header state.
- [x] R5: independent detached windows, taskbar ownership, material and resize contracts.
- [x] R6: responsive title bar, overflow close/status, configurable tab double click/close affordance.
- [x] R7: keychain empty layout, history columns, remembered workbench navigation width.
- [x] R8: configurable instance ownership by user/data directory; rendering restart prompt.
- [x] R9: full Debug/static Release tests, static analysis, QML/translations/assets/structure;
  actual GUI checks and static Release handoff.

## Validation policy

Use targeted tests during each stage; run full gates after all implementation.
All automated GUI runs require a dedicated `--data-dir`. Do not reuse formal
profiles, credentials or histories as smoke fixtures. Existing local history
files remain untouched when removing the obsolete index feature.

## Evidence and remaining issues

Initial audit: SFTP still reads legacy raw Profile credentials; pane header z=12
covers z=10 actions; global zoom ID leaks across workspaces; close performs
thread joins and synchronous callbacks while mutating the session container.
2026-09-12 13:34:47 installed crash dump confirms access violation; exact source
mapping is unavailable without matching release symbols.

## Iteration evidence

- Controller regression: 47 passed, 2 environment-gated skips (Windows Credential
  Manager unavailable in sandbox; interactive real-host reconnect not enabled).
- Settings regression: 27 passed, including schema 31 -> 32 and unrelated provider
  preservation. SFTP regression: 14 passed, including file-only history reads.
- Native window startup smoke passed in `build/test-data/repair-smoke`.
- Terminal render smoke passed in `build/test-data/repair-render-final`, including
  per-workspace zoom/header isolation, detached window ownership/taskbar flags,
  resize/maximize screenshots, reattachment, and actual mouse-header dragging
  from a horizontal split into a vertical split without creating sessions.
- Final static equivalent passed in `build/test-data/repair-render-visible-static`:
  264 frame swaps, maximum event-loop heartbeat gap 18 ms, paint p95 bucket 4 ms,
  texture upload p95 bucket 50 us. Pane drag, zoom isolation, detached resize and
  reattachment all passed. These are test-run observations, not universal bounds.
- Deleted only obsolete completion/index source and tests. Existing user history
  files were neither loaded by that index nor deleted.
- History discovery supports default PowerShell and Git Bash files locally and
  bash/zsh/fish login-shell files over SFTP. Unknown/custom, WSL and Nushell history
  locations are reported unavailable rather than substituted with PowerShell data
  or discovered by executing commands. Session command evidence remains available.
- Full Debug and static Release CTest: 126/126 passed in each tree. Real-host
  and credential-store cases retain their environment-dependent skips.
- Static lifecycle smoke: 8 sequential closes, 3 concurrent terminals; maximum
  close call 261 ms, shutdown 812 ms. Debug under concurrent build/analysis load
  exceeded its timing budget (3418 ms close, 10445 ms shutdown); do not describe
  that run as a passing performance check. The transport stop request itself
  passed 16 repeated startup/idle-reader cycles with caller time below 100 ms.
- Full static responsive layout smoke passed (dark/light, compact/regular,
  Chinese UI, AI Markdown/accessibility and provider recovery). Occluded test
  windows require explicit render/polish before geometry assertions; sleeps alone
  sampled stale geometry. Assertions were retained rather than widened.
- Static material-switching and native resize/hit-test smoke passed.
- Format, 64-file QML quality, 2303 finished translations and code-size/dependency
  checks passed. Full static analysis and focused rechecks of later changes passed.
- Complete static keyboard regression passed in
  `build/test-data/repair-keyboard-final-static`, including native Shell keys,
  history/composer, multiline-paste focus, overflow navigation and final-tab close.
- Runtime tests use dedicated app data and environment directories. Windows
  PSReadLine in this sandbox still attempts its known-folder history path despite
  APPDATA overrides; that write was denied. Final tests set
  `ZTERMY_TEST_SHELL_HISTORY` to an isolated path: generated PowerShell launch skips
  personal profiles and PSReadLine history saving. Normal launches are unchanged.
- Zero-frame failures were traced to inherited Windows `STARTUPINFO/SW_HIDE`:
  Qt reported visible before the native window was shown. The render harness now
  re-shows that hidden test window, verifies exposure and initializes the scene
  graph before sampling. Elevation alone did not fix it; no production renderer
  mode or acceptance threshold was changed to hide the failure.

## Acceptance checklist

### Follow-up: zero-terminal title navigation

- Root cause: `emptyTerminalTitleDragRegion` was stacked over the entire left
  navigation when the terminal-tab count reached zero. Its pointer handler
  intercepted clicks on Workspace, SFTP, Settings/close and the new-tab menu.
  Keyboard/command-palette activation bypassed that region, which explains why
  the application appeared responsive while those mouse actions failed.
- Removed that redundant overlay; the existing blank title-bar drag region and
  native caption hit testing remain unchanged. No terminal, profile or settings
  schema changes are involved.
- Added `--title-navigation-mouse-smoke` and the CMake target
  `ztermy_title_navigation_mouse_smoke`. It uses an isolated data directory,
  sends mouse events through window hit testing (no keyboard input or terminal
  commands), and checks Workspace/SFTP/Settings, Settings close and the new-tab
  menu at 1120 and 600 logical pixels. Mouse helper timestamps now use elapsed
  Windows tick time instead of counters that could manufacture double-clicks.
- Before removal, both widths failed Workspace/SFTP/menu/close. The final Debug
  and static Release runs passed all six checks at both widths, exit 0; evidence:
  `build/test-data/title-mouse-debug-final/logs/ztermy.log` and
  `build/test-data/title-mouse-static-final/logs/ztermy.log`. Window-hit-test and
  workspace-state-store CTests passed in Debug and static Release. QML format/lint,
  C++ format and focused static-Release clang-tidy for `src/main.cpp` passed.
- Release debugging is supported. This static Release uses `/O2 /Ob2 /DNDEBUG`
  without generated debug information; an unrelated PDB must not be used as
  evidence. The existing `msvc-dynamic-relwithdebinfo` preset is the optimized,
  symbol-bearing diagnostic build. The Debug test instance was also inspected
  with CDB without operating the user's original window.

### Manual checks

1. Launch installed and portable builds with different data directories: they
   coexist. Launch the same data directory twice: the first window is activated.
   Disable single-instance behavior in Application settings to allow more windows.
2. Open/close several local and SSH tabs consecutively; repeat while output is
   arriving. Exited local shells show the disconnected state without keeping a
   false connected indicator. Test PS5, PS7 and Nushell separately.
3. Confirm Tab, arrows and Enter retain the Shell's own completion/editing; there
   is no application Ghost text. Open History manually: session commands and
   supported Shell files are labelled and exact duplicates merged. Nothing is
   automatically executed or copied to a persistent global command index.
4. Open SFTP using a managed identity, both from the independent SFTP page and the
   terminal sidebar. Closing the sidebar must survive switching tabs and must not
   disconnect the independent browser. Include key/passphrase and jump-host cases.
5. Copy pane inherits the current endpoint/Shell. New pane offers a profile/Shell
   chooser and defaults to the right. Show headers, drag a pane to another pane's
   center to swap or an edge to change layout; session count stays unchanged.
6. Zoom/header state is independent per workspace. Headers occupy layout space.
   Detached panes have ordinary independent windows, minimize to the taskbar and
   remain independent while switching the main window's tabs. Resize/maximize and
   reattach without losing the session.
7. Narrow the title bar, including Settings and several terminal tabs. Check the
   overflow status/close controls and running-session confirmation; verify tab
   double-click/close-button settings and blank title-bar dragging.
8. Inspect empty keychain categories and history column alignment. Resize/collapse
   the workbench navigation and restart to check remembered widths.
9. Switch solid/acrylic/mica without restarting. Changing the renderer performance
   mode offers restart now/later; restart now closes current connections and
   preserves the same data-directory arguments.

Native DWM composition and real SSH/SFTP authentication still need the owner's
actual desktop/server acceptance. Window flags and captures are evidence for the
implemented contracts, not a substitute for every GPU, Shell profile or host.

## Research references

- [Qt Window transient ownership](https://doc.qt.io/qt-6.8/qml-qtquick-window.html):
  nested windows inherit transient ownership unless explicitly cleared.
- [Qt drag/drop delivery](https://doc.qt.io/qt-6/qml-qtquick-drag.html): a drag must
  deliver a drop; moving a visual or showing a preview alone does not rearrange it.
- [PSReadLine history](https://learn.microsoft.com/en-us/powershell/module/psreadline/about/about_psreadline):
  the Shell owns its saved history; ztermy does not force a flush.
- [PSReadLine history options](https://learn.microsoft.com/en-us/powershell/module/psreadline/set-psreadlineoption):
  smoke-only isolation uses an explicit history path and `SaveNothing` before input.
- [Nushell history paths](https://www.nushell.sh/book/special_variables.html):
  a history path may refer to text or SQLite. Unsupported formats are not parsed as
  another Shell's text history.
