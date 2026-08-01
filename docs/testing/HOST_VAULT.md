# Host Vault manual verification

> This document preserves the accepted V1/V1.1 baseline. V1.2 credential
> persistence and updated host flows are verified by
> [V1_2_CREDENTIALS.md](V1_2_CREDENTIALS.md).

Run the dynamic Debug build on Windows 11 and open **Hosts** from the custom
title bar.
Use non-sensitive test endpoints and never place a real password or passphrase
in the profile name, group, host, username, or key-path fields.

## Quick Connect and recent connections

1. Enter an invalid target such as `missing-user` and activate **Connect**.
2. Enter `user@host`, `user@host:2222`, and a bracketed IPv6 target such as
   `user@[2001:db8::1]:2222`.
3. Open the authentication dialog with the keyboard, switch between
   private-key and password authentication, then press Escape.
4. Resize the open dialog to the minimum window size and Tab through every
   visible field and both actions.
5. Start an unsaved connection and deliberately fail authentication.
6. Explicitly save a Quick Connect target, fail authentication, then connect
   that saved profile successfully.
7. Restart ztermy.

Expected:

- Invalid targets remain on Hosts with a precise inline error.
- Valid targets parse to the expected username, host, and port; unbracketed
  IPv6 is rejected.
- The dialog opens on **Authentication**, remains vertically scrollable at
  minimum size, and Escape restores focus to the action that opened it.
- **Save as a reusable host profile** is off by default.
- Passwords and private-key passphrases disappear after submission and never
  appear in saved profile JSON or logs.
- Failed and unsaved connections never appear under **Recent connections**.
- A saved profile appears in Recent only after authentication succeeds, and
  remains there after restart.
- Recent contains at most six saved profiles in newest-first order. Editing
  preserves recency; duplicating starts without recency.

## Layout and editor

1. With no profiles saved, verify the empty state and expanded profile editor.
2. Save the first profile and select **New host**.
3. Cancel the new profile, then edit an existing profile.
4. Resize the window from its minimum size through maximized.

Expected:

- Search is the primary control and the visible profile count stays aligned.
- The empty state explains the next action without obscuring the editor.
- Empty and no-match states share the same card geometry, wrap their guidance,
  and expose a meaningful accessible description.
- Saving or cancelling collapses the editor; **New host** and **Edit** expand
  it with focus in the profile-name field.
- Host cards show endpoint and authentication type without exposing secrets.
- Hovering a card changes only its color and border; surrounding content does
  not move.
- Controls remain reachable and the page scrolls instead of clipping at small
  window sizes.

## Create and group

1. Select **New host** and save two private-key profiles in different groups.
2. Save a password profile in one of the same groups.
3. Leave the group empty on a fourth profile.

Expected:

- Profiles appear below case-insensitively sorted group headings.
- Profiles within each group are sorted by name.
- The empty group appears as `Ungrouped`.
- The profile counter reflects the number of visible profiles.
- Passwords and private-key passphrases are not required when saving.

## Search

Search separately for a profile name, group, host, username, port, and the
authentication terms `password` and `private-key`. Then enter a query with no
matches and finally clear the search.

Expected:

- Matching is case-insensitive across every listed field.
- Only matching cards remain and the visible counter updates.
- A no-results message appears without changing editor state.
- Clearing the query restores every group and profile.

## Edit and copy

1. Edit a profile's name, group, endpoint, and authentication method.
2. Save and restart ztermy.
3. Copy the edited profile twice.

Expected:

- Editing preserves the profile identity and moves the card to its new group.
- The edit survives restart.
- Copies receive new identities and names ending in `copy` and `copy 2`.
- Copies preserve only non-secret connection settings.
- Connecting a password profile or passphrase-protected key still asks for the
  credential for that attempt.

## Delete

1. Open the delete confirmation and cancel it.
2. Open it again and confirm deletion.
3. Restart ztermy.

Expected:

- Cancel leaves the profile unchanged.
- Confirm removes only that profile; trusted host keys remain untouched.
- The deletion survives restart.

## Password authentication

1. Save a password profile without entering a password.
2. Select **Connect**, enter the password in the per-attempt credential
   dialog, and accept the independently verified host key if it is new.
3. Close the connected tab and connect the saved profile again.
4. Make one connection attempt with an intentionally incorrect password, then
   retry with the correct password.
5. Restart ztermy and connect the profile once more.

Expected:

- Saving never asks for or stores the password.
- The credential dialog masks input, clears it after cancel or submission, and
  is required on every new connection attempt.
- A new host key is confirmed before authentication; the already trusted key
  does not prompt again.
- The incorrect password reports `SSH authentication was rejected` without
  removing the profile or trusted host key.
- The correct password opens an interactive terminal in every successful
  attempt.
- Restart preserves only the non-secret profile and host trust.

## Connection failure presentation

Use only endpoints that you own or are authorized to test.

1. Connect a private-key profile with an intentionally invalid username.
2. Connect a profile to a known unused port on `127.0.0.1`.
3. Connect to a controlled endpoint that accepts TCP but does not send an SSH
   banner.
4. Connect a valid profile and enter `exit` in the remote shell.

Expected:

- The four outcomes are described respectively as authentication rejected,
  connection refused, connection timed out, and remote host closed.
- Each failure returns the session UI to a stable disconnected state and
  exposes a sensible retry or profile-edit action.
- No case crashes, retries indefinitely, or leaves the Hosts workflow blocked.
- Logs contain classification and timing information only; they contain no
  password, passphrase, private-key content, or terminal input.

## Secret-storage check

After the tests, inspect:

```text
%LOCALAPPDATA%\ztermy\ztermy\profiles.json
```

Expected:

- The file contains profile names, groups, endpoints, authentication method,
  private-key paths, and whether a passphrase prompt is needed.
- It contains no password, private-key passphrase, private-key content, or
  terminal input.
