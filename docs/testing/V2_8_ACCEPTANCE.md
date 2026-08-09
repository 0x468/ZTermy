# V2.8 acceptance: SFTP and transfer workflow closure

## Automated evidence

- MSVC dynamic Debug build passed; CTest passed 44/44 in 41.33 seconds.
- C++ formatting and full clang-tidy passed with warnings treated as errors.
- QML formatting/qmllint passed for 41 QML files; the translation gate passed
  for 847 complete source/translation pairs.
- Real-host Debug and static Release GUI gates passed against the dedicated
  trusted key fixture:
  - remote home, permission-error preservation, compact/wide toolbar;
  - size sorting, visible columns, directory-first order;
  - GB18030 then UTF-8 SFTP restart;
  - 64 MiB upload pause/resume and 64 MiB download pause/resume;
  - completed file size, copy path, drag availability, remote cleanup, and
    orderly tab/session close;
  - visually inspected captures: `real-host-transfer-paused.png` and
    `real-host-transfer-completed.png` in each isolated runtime test-data tree.
- MSVC static Release build passed; CTest passed 44/44 in 46.62 seconds.
- All eight real-window Release gates passed serially, including the 100%,
  125%, 150%, and 200% DPI matrix.
- The WiX installer contract passed. WiX reported its retained ICE61, ICE69,
  and ICE91 warnings; these are warnings rather than contract failures.
- Release artifacts:
  - `ztermy-0.2.8-windows-x64-portable.zip`: 18,901,067 bytes,
    SHA-256 `68924548d0de64a6062efdfc86e8aa3d59a22f08f038e6e183bf3a80f3afe09e`;
  - `ztermy-0.2.8-windows-x64.msi`: 15,294,464 bytes,
    SHA-256 `1123fe8cb48029e4e41466629c27322c383e96e13f01eec0dd3212f5bc5646df`.

No password, passphrase, private-key content, terminal input, or unredacted
secret-bearing command line is recorded in this document or application logs.

## Manual acceptance retained for daily use

1. Upload and download a multi-gigabyte regular file. Pause for at least ten
   seconds, restart ztermy, unlock credentials if required, resume, and compare
   a SHA-256 hash with the source.
2. Pause a transfer, cancel it, and confirm the visible task becomes cancelled
   without later progress. Remove a failed upload record and confirm a repeat of
   the same destination does not find a stale `.ztermy-part-*` file.
3. Drag a completed download row into Explorer and confirm Windows performs a
   normal file copy. Direct drag from an undownloaded SFTP row is intentionally
   unsupported.
4. Switch a compatible server between UTF-8 and GB18030 filename mode and verify
   Chinese names across list, navigation, rename, create, upload, and download.
5. Configure columns, sorting, and directory-first order, close/reopen the saved
   host, and confirm the workspace choices persist.
