# V2.6 acceptance: remote telemetry

## Automated evidence

- Debug and static Release CTest: `43/43` passed on 2026-08-09. This includes
  `remote-telemetry`, session/controller integration, translation, native-window
  smoke, executable metadata, and interface asset contracts.
- C++ formatting, QML formatting, `qmllint`, and all `94` clang-tidy translation
  units passed with warnings treated as errors.
- The opt-in static Release real-host case
  `collectsRemoteTelemetryOnRealHost` passed against the approved Linux SSH
  fixture: `3` QtTest cases passed in `6225 ms`. The case verifies a usable
  telemetry sample, history-request priority, and polling pause after visibility
  loss.
- The self-contained portable package was created at
  `build/msvc-static-release/package/portable/ztermy-0.2.6-windows-x64-portable.zip`
  (`18806511` bytes, SHA-256
  `d9d885f74711099359485b35f47f9b58fc66e194bf728413817d090bbfc0f5f9`).
- The per-user MSI was generated at
  `build/msvc-static-release/ztermy-0.2.6-windows-x64.msi` (`15216640` bytes,
  SHA-256
  `6e7e768b944848df8465443b4aee122bc9e6070a87dcdf9bc95033e853763250`).
  WiX ICE validation remains blocked by this development machine's documented
  non-elevated Windows Installer service error `WIX0217`/exit `217`; compilation,
  MSI generation, portable packaging, and both CTest matrices are unaffected.

## Manual acceptance

Use the existing Linux SSH test host or another trusted Linux host.

1. Open an SSH terminal and wait for two samples (normally within 10 seconds).
   CPU, memory, disk, network, and SSH latency appear without any command or
   marker appearing in the terminal or shell history.
2. Focus each compact metric with mouse and keyboard. Its detail surface uses the
   current light/dark theme, remains readable, and exposes recent trend plus the
   relevant bounded details.
3. Run CPU/network activity on the host. Values and trends update without typing
   latency, dropped terminal output, flicker, or layout jumps.
4. Open history while a telemetry probe is due. History loads normally; telemetry
   resumes later and the interactive prompt remains untouched.
5. Switch to another terminal, Hosts, or Settings; minimize/deactivate ztermy;
   wait at least 10 seconds. Hidden sessions must not continue polling. Returning
   to the SSH terminal resumes sampling.
6. Resize from narrow to wide widths. Lower-priority telemetry values collapse
   cleanly before toolbar actions, with no overlap or inaccessible button.
7. Disconnect or close the tab while details are open or a probe is active. The
   popup closes, shutdown completes promptly, and no crash/assertion occurs.
8. Connect to an unsupported/non-Linux target. The terminal remains fully usable;
   monitoring shows a quiet unavailable state and stops retrying after its bound.
