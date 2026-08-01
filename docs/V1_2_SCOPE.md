# ztermy V1.2 scope

Status: complete

## Goal

V1.2 makes saved-host authentication practical without weakening the V1
security boundary. It also brings the Hosts dashboard and new-host workflow
closer to the NetCatty reference while retaining ztermy's native Qt/C++
implementation and existing host cards.

## Included

1. Save submitted SSH passwords and private-key passphrases immediately,
   without waiting for a successful connection.
2. Use Windows Credential Manager by default in installed mode.
3. Use a master-password-protected encrypted vault in portable and custom-data
   modes.
4. Offer session-only storage, global store selection, verified migration,
   optional source cleanup, per-host forgetting, and remove-all cleanup.
5. Keep only an opaque credential reference in `profiles.json`.
6. Default an empty profile name to the hostname and provide an auto-selected
   editable name in the new-host form.
7. Keep the existing ztermy saved-host cards while moving the host form into a
   responsive in-layout right-side detail pane. Opening it narrows and reflows
   the Hosts master area; clicking the master area dismisses it. Align its
   hierarchy, password-first default, secure-save toggle, feedback, and
   keyboard behavior with the NetCatty reference. **Save profile** persists
   without connecting; **Connect** saves the profile and chosen credential
   preference before starting SSH.
8. Start on the Hosts workspace without creating an unsolicited local terminal
   session. An initialized portable vault prompts for its master password once
   at startup; dismissing the prompt leaves a persistent, keyboard-accessible
   title-bar unlock entry.
9. When the user explicitly edits a profile, retrieve its credential from the
   active unlocked backend into a masked field with an accessible show/hide
   action. Clear the field when the editor closes or authentication changes.

## Security invariants

- Secrets, master passwords, derived keys, private-key contents, and terminal
  input are never logged.
- The portable vault uses scrypt (`N=32768`, `r=8`, `p=1`) and AES-256-GCM
  with a fresh salt/nonce and atomic file replacement.
- A migration does not activate the destination until every copied credential
  reads back successfully.
- Authentication failure does not delete or rewrite a stored credential.
- Duplicating a profile does not duplicate its credential.
- Deleting a profile removes its referenced credential by default.

## Deferred

- Cross-platform keychain backends for Linux and macOS.
- Credential sharing or synchronization.
- Recovery of a forgotten portable-vault master password.
- Per-profile appearance settings.
- The English/Chinese Qt translation pipeline and runtime language selection.
  This is planned as the V1.3 foundation before adding more text-heavy
  workflows; see ADR 0022.
- NetCatty serial, Telnet, Mosh, AI, or cloud-sync features.

## Acceptance

Follow [testing/V1_2_CREDENTIALS.md](testing/V1_2_CREDENTIALS.md). V1.2 is
complete only after automated gates, an interactive Windows Credential Manager
round trip, portable restart/migration checks, saved password/key connections,
and dark/light normal/narrow UI review pass. Automated results and the remaining
human evidence are tracked in
[testing/V1_2_ACCEPTANCE_EVIDENCE.md](testing/V1_2_ACCEPTANCE_EVIDENCE.md).
