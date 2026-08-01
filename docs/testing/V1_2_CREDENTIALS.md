# V1.2 credential and host workflow acceptance

Never record a real password, passphrase, master password, private-key content,
or secret-bearing screenshot in the evidence.

## A. Installed-mode Windows store

1. Start the installed Debug build without `portable.flag`.
2. Open **Hosts**, create a password profile, leave **Profile name** empty,
   enter a disposable test password, leave **Save credential securely** on,
   and save without connecting.
3. Close and reopen ztermy, then connect that host.
4. In Windows Credential Manager, confirm one generic credential named
   `ztermy:ssh:<profile-id>:password` exists; do not reveal its value.
5. Intentionally enter a wrong password and choose **Connect** without pressing
   **Save profile** first.
6. Create a private-key profile with the authorized `id_ed25519` fixture and
   verify it still connects after a restart. If that key is passphrase
   protected, save its passphrase before connecting and confirm the matching
   `ztermy:ssh:<profile-id>:key-passphrase` generic credential exists.
7. Edit the saved password profile. Confirm the Password field is already
   populated but masked, then use the eye action with mouse and keyboard to
   show and hide it. Close and reopen the editor to confirm it starts masked.
8. Open **New host**, enter a hostname while leaving **Profile name** empty,
   then click the generated profile name. Confirm the entire name remains
   selected after releasing the mouse. Type a custom name, move focus away,
   and click the name again; confirm it now uses normal caret placement rather
   than forcing another full selection.
9. With **New host** or **Edit profile** open, confirm the detail pane occupies
   the right side of the Hosts layout and causes the host cards to reflow.
   Click an empty point in the left Hosts area and confirm the detail pane
   closes. Reopen it and verify **Close** and **Cancel** still work.

Expected: the profile name becomes the hostname; **Save profile** succeeds
without a network attempt; **Connect** saves the profile and credential before
starting SSH; restart does not prompt for the saved password; an incorrect
password reports authentication rejection but remains saved until replaced or
forgotten. The private-key profile also survives restart, and a protected key
does not prompt again while its saved passphrase remains available. Explicit
editing reads the active saved value into a masked field; the eye action never
reveals it without direct user input. An automatically generated profile name
stays fully selected through mouse release until the user supplies a custom
name. The editor behaves as a dismissible master-detail pane, not an overlay.

## B. Profile lifecycle

1. Duplicate the profile.
2. Expand the duplicate and connect it.
3. Use **Forget secret** on the original and confirm the dialog.
4. Save the credential again, then delete the original profile.

Expected: the duplicate has no saved credential and prompts; forgetting keeps
the host but removes its credential; deleting removes both the profile and its
credential.

## C. Portable vault

1. Run the portable package and open **Settings > Security**.
   Alternatively, open **New host** first and use its **Open Security** action;
   saving a credential before vault creation must explain that creation is
   required rather than claiming an existing vault merely needs unlocking.
2. Before creating the vault, migrate to Session and confirm that leaving the
   empty Portable store does not require a master password.
3. Create a portable vault with a master password of at least eight characters,
   then migrate back to Portable.
4. Save one disposable password profile, close ztermy, and reopen it.
5. Confirm ztermy opens on Hosts without creating a PowerShell tab and presents
   the portable-vault unlock dialog. Choose **Not now**, verify the title-bar
   lock indicator remains, then use it to reopen the dialog.
6. Attempt to unlock with a wrong and the correct master password.
7. Change the master password, restart, and verify only the new password works.
8. Copy the complete portable directory to another temporary folder and verify
   that the same master password unlocks it there.

Expected: an uninitialized empty vault can be left without creating it; once a
vault exists it must be unlocked before migration. The vault starts locked
after every launch; startup offers a direct unlock without forcing navigation
to Settings, and a dismissed prompt leaves a persistent unlock entry. Wrong
passwords do not alter it; the correct password restores saved-credential
connections; the master password is never persisted; `credentials.zvlt`
contains no readable password text.

## D. Session-only storage

1. Migrate a saved system credential to Session with source cleanup disabled.
2. Confirm the host connects during the current process, then close and reopen
   ztermy.
3. Connect the same host, then switch the active store back to System.

Expected: the credential works only until the process exits; after restart the
host is shown as missing a credential and opens the credential prompt instead
of failing a stale lookup. Switching back to System discovers the retained
system copy and restores saved-credential connection behavior.

## E. Migration and cleanup

1. With at least one saved credential, migrate System -> Portable with source
   cleanup enabled.
2. Restart, unlock, and connect.
3. Migrate Portable -> System, first with source cleanup disabled and then back
   with cleanup enabled.
4. In **Credential cleanup**, clear an inactive retained store and confirm the
   active credential still works; then clear the active store.

Expected: each migration reports success only after verification; the selected
store survives restart; disabled cleanup leaves a recoverable duplicate;
enabled cleanup removes the previous copy; clearing an inactive store does not
detach active host credentials; clearing the active store preserves host
profiles but makes each one prompt for a credential.

## F. UI and accessibility

Review Hosts and **Settings > Security** in dark/light themes at normal and
narrow widths. Use Tab, Shift+Tab, Space, Enter, arrows, and Escape without a
mouse.

Expected: focus is visible; profile-name auto-fill selects all only until the
user customizes it, and the selection remains stable after the initiating mouse
click; secret fields stay masked until their explicit eye action is used;
confirmations are keyboard operable; success/error text does not rely on color
alone; no content clips or overlaps. Empty portable-vault password fields state
the eight-character minimum and short input explains why the action is
disabled.

## G. Automated gates

From a Visual Studio developer environment:

```powershell
cmake --build --preset msvc-dynamic-debug
ctest --test-dir build\msvc-dynamic-debug --output-on-failure
cmake --build --preset msvc-dynamic-debug --target ztermy_ui_layout_runtime_smoke
cmake --build --preset msvc-dynamic-debug --target ztermy_ui_keyboard_runtime_smoke
```

The Windows Credential Manager unit and AppController integration cases may
skip in a service/non-interactive logon session. Run them from an interactive
desktop session or replace that evidence with the check in section A.
