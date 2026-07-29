# Host Vault manual verification

Run the dynamic Debug build on Windows 11 and open **Hosts** from the sidebar.
Use non-sensitive test endpoints and never place a real password or passphrase
in the profile name, group, host, username, or key-path fields.

## Create and group

1. Save two private-key profiles in different groups.
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
- A no-results message appears without hiding the editor.
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
