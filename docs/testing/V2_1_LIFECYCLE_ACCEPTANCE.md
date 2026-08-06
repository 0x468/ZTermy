# V2.1 lifecycle acceptance

Use the latest `msvc-static-release` build. These checks need a real SSH/SFTP
host and are intentionally not part of unattended CTest.

## A. Active SFTP tab close

1. Connect to an SSH profile and open SFTP.
2. Refresh or navigate a directory with enough entries to keep the request
   visibly active.
3. Close only that terminal tab and continue using another tab.

Expected: the tab disappears immediately, the remaining UI stays interactive,
and ztermy does not freeze, terminate, or show a runtime assertion while the
SFTP worker finishes in the background.

## B. Application exit with mixed activity

1. Open two or more terminal tabs.
2. Start session logging in one tab and SFTP browsing in another.
3. Begin a sufficiently large upload or download, then close ztermy while the
   transfer is still active.

Expected: the application exits without a hang, crash, assertion dialog, or
orphaned ztermy process. The session log can be opened afterwards and contains
all output delivered before the terminal backend stopped.

## C. Interrupted transfer recovery

1. Relaunch ztermy after check B and open the transfer panel.
2. Locate the transfer that was active during exit.
3. Retry it explicitly.

Expected: the prior task is shown as interrupted/failed and retryable, not as a
completed transfer or a user-cancelled task. Retry starts a new worker and can
finish normally. No transfer starts automatically on launch.

## D. Repeated normal exit

Launch and close ztermy several times with no active transfer, then repeat with
an idle SFTP panel and with session logging enabled.

Expected: every exit is clean and does not create duplicate transfer entries,
corrupt the recovery file, or leave a process behind.
