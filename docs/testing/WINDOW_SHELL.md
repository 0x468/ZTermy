# Window shell manual test

Status: required before the window-shell milestone is accepted

Run the dynamic Debug executable:

```text
build/msvc-dynamic-debug/ztermy.exe
```

Perform the checks on Windows 11 with the system Snap windows setting enabled.
Repeat the DPI checks on every available monitor.

## Caption commands

1. Click minimize.
   Expected: the window minimizes to the taskbar and restores from the taskbar.
2. Click maximize.
   Expected: the window fills the current monitor work area without covering
   the taskbar; the glyph changes to Restore.
3. Click restore.
   Expected: the previous window geometry returns and the glyph changes to
   Maximize.
4. Double-click empty title-bar space twice.
   Expected: the first double-click maximizes and the second restores.
5. Click close.
   Expected: the process exits normally.

## Windows 11 Snap Layouts

1. Restore the window if it is maximized.
2. Keep the pointer stationary over the maximize button for about one second.
   Expected: the native Windows 11 Snap Layouts flyout appears.
3. Select a zone.
   Expected: ztermy snaps to that zone and Snap Assist offers other windows.
4. Press `Win+Z`.
   Expected: the same layout chooser appears and accepts keyboard selection.

If the flyout does not appear, first verify **Settings > System >
Multitasking > Snap windows** is enabled. Record the Windows build, display
resolution, scaling percentage, and whether other applications show the
flyout.

## Native resize

1. Move the pointer over each edge and corner.
   Expected: the matching horizontal, vertical, or diagonal resize cursor
   appears.
2. Drag each edge and corner.
   Expected: resizing remains smooth and stops at 500 by 360 effective pixels.
3. Maximize the window and repeat the edge check.
   Expected: resize cursors are not offered while maximized.

## DPI and monitor behavior

1. Drag the window slowly between monitors with different scaling.
   Expected: text remains sharp, the pointer stays attached to the title bar,
   and the window does not jump or change physical size unexpectedly.
2. Maximize and restore once on each monitor.
   Expected: each monitor's work area and previous geometry are respected.

Report each section as pass or fail. For a failure, include the exact step,
monitor resolution and scaling, Windows build, and a screenshot or short
recording when practical.
