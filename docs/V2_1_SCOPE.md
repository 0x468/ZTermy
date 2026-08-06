# V2.1 daily-use stabilization

Status: in progress on the `0.2.1` line

## Release identity

V2.1 is a compatible maintenance release over the accepted `0.2.0` baseline.
It retains the V2 codename `此` and the approved verse because ztermy codenames
belong to the `x.y` line, not individual patch builds.

## Priorities

1. Make failures diagnosable without collecting terminal input, credentials,
   host data, command history, log contents, or crash dumps automatically.
2. Harden long-running SSH, SFTP, transfer cancellation, tab close, and app
   shutdown behavior from reproducible daily-use evidence.
3. Improve installer and portable-package diagnostics while preserving the
   existing per-user installation and portable storage contracts.
4. Finish visual consistency using ztermy-owned vector assets and retain the
   English/Chinese, keyboard, and accessibility contracts.

## First slice: privacy-safe diagnostics

The Application settings page can export a small JSON environment summary and
open the local log or crash-report directory. The JSON contains application,
Qt, Windows/kernel, CPU architecture, storage-mode, and artifact count/size/time
metadata only. It never embeds artifact names, local paths, contents, saved
profiles, credentials, terminal data, or command history.

Crash dumps are deliberately excluded because a dump can contain process memory.
The owner must review and choose a dump explicitly before sharing it.
