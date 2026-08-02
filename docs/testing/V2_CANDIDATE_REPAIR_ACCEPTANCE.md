# V2 candidate repair acceptance

This checklist covers the first repair pass after the `0.2.0` V2 candidate.
All resulting V2 artifacts remain in the `0.2.x` version line. V3 alone starts
at `0.3.0`.

## Automated evidence

- Build the dynamic Debug application and focused SFTP/transfer/workspace tests.
- Run `translation-catalog`, `transfer-queue`, `sftp-session`,
  `sftp-directory-model`, `transfer-manager`, `workspace-state-store`,
  `app-controller`, and `qml-native-window-smoke`.
- Run the complete non-real-host test suite before creating the candidate
  commit and static release artifacts.

## Owner acceptance

1. Open **New host**. In **Group**, type the first character of an existing
   group. The remaining matching suffix is selected, continued typing updates
   the completion, the drop-down lists existing groups, and a new custom value
   remains valid.
2. Collapse **Recent connections** and one saved-host group, restart ztermy,
   and confirm both remain collapsed. Search temporarily exposes matching
   profiles. **Clear recent connections** removes only timestamps and never
   deletes saved profiles.
3. In a terminal, confirm the direct toolbar order is SFTP, composer, search,
   session log, scripts, then more. Every direct action has a tooltip; command
   history appears only in **More**. Starting a log proposes
   `<tab>_yyyy-MM-ddThh-mm-ss.log`.
4. Connect to an SSH profile and open SFTP. It starts in that account's home
   directory, **Home** returns there, and `..` opens the parent. Enter an
   inaccessible path: the previous successful listing remains visible with an
   error banner, and retry/navigation remain usable.
5. Start one upload and one download. Cancel each and observe **Cancelling**
   until the worker finishes. The cancel control remains clickable before the
   request, completed/failed/cancelled rows can be removed, and **Clear
   finished transfers** removes terminal records only.
6. While an SFTP listing or transfer is active, close its terminal tab and then
   close ztermy. The UI must not hang, the process must exit without a CRT/heap
   dialog, and reopening must recover interrupted transfer metadata safely.
7. Verify About, PE metadata, portable archive, MSI, and manifest still use a
   `0.2.x` version. No V2 artifact may identify itself as `0.3.0`.
