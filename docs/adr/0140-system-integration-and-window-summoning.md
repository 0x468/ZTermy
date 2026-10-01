# ADR 0140: Window summoning and owned Windows integrations

- Status: Accepted
- Date: 2026-10-01

## Decision

Global window shortcuts use Windows RegisterHotKey with MOD_NOREPEAT, not a
keyboard hook or terminal-input interception. They hide the foreground main
window and otherwise restore and focus it, preserving its maximized state.
Detached windows are unaffected. The binding is disabled by default; a conflict
must preserve the previous registration and must not save the failed binding.
Cursor-monitor summoning is optional and defaults to preserving placement.
The binding belongs to the dedicated shortcuts page, in a system-wide section,
and shares its keyboard recording, immediate-save, clear and reset conventions.
Other window-setting drafts must not overwrite a binding saved there.
Application settings schema 42 adds these preferences and migrates schema 41
without changing unrelated settings.

`--open-directory <path>` opens a default local shell tab in that directory. The
path is an argument/working directory, never injected shell input. Existing
instances receive a bounded, versioned local request. Requests received during
startup wait until the controller and main view are ready. `--background` is
reserved for login launches; an explicit directory takes precedence. A hidden
launch requires an available notification-area icon, otherwise the window is
shown so the user cannot lose access.

Traditional Explorer verbs, startup registrations and sparse identity packages
are typed ZInstaller integrations, with selections, ownership, rollback and
uninstall support. Application settings must not independently mutate installer
owned registrations. Windows 11 modern menu integration uses a small native
IExplorerCommand extension and a signed external-location identity package.
Initial scope is the invoking user, not automatic provisioning for all users.

Internal distribution may use a self-signed signing certificate. Only its public
certificate is distributed to end users. By the owner's explicit choice,
local signing material is stored in the repository-root `.signing/` directory,
which must be Git-ignored and is never part of installer/release payloads.
The owner also chose a passwordless PFX for private PVE storage; this file grants
signing authority and must have restricted access, not be published or committed.
Export requires an explicit `-ExportPrivateKeyWithoutPassword` switch and verifies
the Git-ignore/no-tracked-files guard first. Trust requires explicit consent
independent of a default-on menu selection. Host certificate trust must not be
changed by acceptance tests.
Certificates shared by products are not removed when a single product is
uninstalled. Sandbox acceptance precedes host deployment; successful sandbox
tests do not establish multi-monitor or enterprise-policy compatibility.

The identity builder signs packages when explicitly given the personal-store
certificate thumbprint; it exports only the public CER alongside the package.
No pre-existing owner certificate is assumed;
creating a development certificate and trusting it on a device are separate steps.
Windows SDK 26100's modern-menu schema accepts Directory and
Directory\\Background, not Drive; drive verbs remain traditional integrations.
On the tested Sandbox build 26100, CurrentUser/TrustedPeople alone still yields
0x800B0109, while explicitly trusting LocalMachine/TrustedPeople permits
deployment. Do not assume every Windows build has the same trust behavior.

Implementation is staged: traditional menus/startup and current-user sparse
packages have typed installer transactions and upgrade-choice persistence.
Identity packages unregister before external binaries are replaced; rollback
restores old files before the old registration. Schema 4 preserves manifests
and ledgers 1–3; journal 3 preserves journals 1–2. The platform helper is fixed
runtime code with base64-encoded JSON data, never a product-provided script.
Modern menus are opt-in. The owner explicitly approved developer-certificate
trust: LocalMachine/TrustedPeople, never Root,
machine-wide impact, administrator authorization and shared-trust retention on
uninstall. After feedback, the independent trust toggle is removed. Selecting
the menu does not itself grant trust. A background read-only preflight precedes
installation and removal; only missing trust opens a contextual confirmation
page showing publisher and the complete SHA-256 fingerprint. Returning preserves
the draft. Already trusted signatures proceed without another prompt.
The fixed elevation helper receives immutable public DER bytes and revalidates
fingerprint, non-CA/code-signing usage and validity before adding that certificate.
Consent is never remembered across installs; an existing trusted identity needs
no new consent. No consent means normal Windows signature rejection and rollback.
Current-user identities reject per-machine installation to avoid accidentally
registering under different UAC credentials. In-app integration
maintenance remains a separate, unimplemented part of the original scope.

## Verification

Test literal Unicode/metacharacter paths, malformed/oversized IPC, startup
delivery, native hotkey conflicts, disabled bindings, schema-41 preservation,
foreground/background transitions and shutdown registration release. Installer
acceptance uses clean Sandbox sessions for trust refusal/approval, menus,
upgrades, rollback and cross-product uninstall. Login and multi-monitor behavior
require additional runtime evidence.

2026-10-01 evidence: native RegisterHotKey runtime hide/wake and maximize/minimize
recovery passed with an unchanged peer window; second-process directory forwarding
opened the real PowerShell terminal in a Unicode/metacharacter directory. Seven
focused CTest targets, QML/format gates and touched-platform static analysis passed.
ZInstaller owning-module tests/Clippy and schema-3 SDK product validation passed.
Sandbox verified traditional-menu upgrade failure rollback and cleanup, then signed
identity rejection/trust/COM invocation/removal. Windows MCP verified actual modern
Explorer folder-selection and folder-background menu display and literal-directory
invocation against a protocol sink. Full Qt package installation, generic identity
installer transactions, production signing, login and multi-monitor acceptance
remain separate gates; these results do not mean the whole integration is finished.

Later package evidence: Debug and static Release each passed 130 CTest cases;
full static Release clang-tidy and format/QML gates passed. The real Setup passed
clean Sandbox installation with 140 verified file hashes, traditional menus,
selected background-login shortcut, reinstall, native/UI integration smoke and
uninstall. Selecting an untrusted modern identity failed with Windows 0x800B0109
and restored the previous ledger without changing trust. Context-bound callback
delivery uses QTimer singleShot without an unnecessary outer queued wrapper;
the lifetime probe verified delivery/cancellation and captured-payload release
over 100 cycles.

Final authorized package evidence: the clean Sandbox Setup passed explicit
LocalMachine/TrustedPeople consent (no Root import), trusted sparse registration,
reinstall without renewed consent, a version-changing backend upgrade and native
COM activation. An injected wrong publisher after a managed-file modification
failed registration and restored the prior file, ledger and package registration.
Uninstall removed menus, startup and managed files, retaining unknown user files
and shared certificate trust. The real installed Qt native/UI smoke passed again;
Windows MCP screenshots initially verified the separate default-off consent row.
The later merged flow removes that row; screenshots verified the real packaged
options page and production contextual-confirmation component. Its read-only
preview runs on the main thread with no installation executor, rather than
inside a unit-test worker (the native event loop requires the main thread).
Sandbox acceptance reads UTF-8 records explicitly and uses a unique runtime data
directory per run; previous captures must never restore settings into a fresh
acceptance fixture. Results identify the tested Setup hash and start time.
Acceptance never imports or deletes host trust. Actual login, multi-monitor
behavior and in-app installer integration maintenance remain separate work.

Merged-flow Setup acceptance started at 2026-10-01T14:47:58Z and passed the
complete clean-Sandbox sequence, including native/UI smoke and uninstall.
The result records Setup SHA-256
`a20278bf1ae856bded39536d206a5ad4fedba6d4133003bf83b630d84ab901c2`.
Focused installer tests passed 65 cases, with Clippy and formatting checks clean.
