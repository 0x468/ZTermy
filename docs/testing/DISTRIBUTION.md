# Distribution manual verification

## Build the static portable archive

Start an x64 Visual Studio developer shell and set the static Qt root:

```powershell
$env:ZTERMY_QT_STATIC_ROOT = "D:\qt-self-built\qt-6.8.3-static"
cmake --preset msvc-static-release --fresh
cmake --build --preset msvc-static-release
ctest --test-dir build/msvc-static-release --output-on-failure
cmake --build --preset msvc-static-release --target ztermy_portable_package
```

If Ghostty's pinned source is already available locally, an offline configure
may reuse it:

```powershell
cmake --preset msvc-static-release `
  -DFETCHCONTENT_SOURCE_DIR_GHOSTTY=D:\path\to\the\pinned\ghostty\source
```

The archive is written below:

```text
build/msvc-static-release/package/portable
```

Expected:

- Configuration fails if `Qt6::Core` is not a static library.
- All tests pass.
- The archive contains one versioned directory with `ztermy.exe` and
  `portable.flag`.
- `dumpbin /dependents ztermy.exe` lists Windows system DLLs only; it does not
  list Qt or OpenSSL DLLs.

## Portable runtime isolation

1. Extract the archive into a new writable directory.
2. Start `ztermy.exe`.
3. Create a host profile without entering a password or passphrase.
4. Close ztermy.
5. Inspect the extracted directory.

Expected:

- The application starts on a machine without Qt on `PATH`.
- A sibling `data` directory contains `profiles.json`, `logs`, and `crashes`.
- `known_hosts.json` appears only after host trust is remembered.
- The newest log contains `storageMode= portable` and points to the extracted
  directory.
- No ztermy data is created or modified below `%APPDATA%` or `%LOCALAPPDATA%`.
- Closing produces no runtime assertion or lingering process.

## Build and inspect the per-user MSI

Start from the same static Release build used for the portable archive:

```powershell
dotnet tool install --tool-path build/tools/wix wix --version 4.0.4
$env:WIX = "$PWD/build/tools/wix"
cmake --build --preset msvc-static-release --target ztermy_installer
& "$env:WIX/wix.exe" msi validate `
  build/msvc-static-release/ztermy-0.1.0-windows-x64.msi
```

If `build/tools/wix` already contains WiX 4.0.4, omit the install command. The
MSI is written to:

```text
build/msvc-static-release/ztermy-0.1.0-windows-x64.msi
```

Expected:

- The package contains only `ztermy.exe`; it contains no Ghostty development
  files, Qt DLLs, OpenSSL DLLs, PDBs, or `portable.flag`.
- The installation scope is `perUser`, and the application directory is below
  the current user's local application data directory.
- A direct `ztermy` shortcut is authored in the current user's Start menu.
- ICE validation completes with no errors. CPack currently produces three
  reviewed warnings: ICE61 for deliberate same-version upgrades during V1
  testing, ICE69 for its shortcut component referencing the executable
  component in the same feature, and ICE91 because this package is fixed to
  per-user scope rather than switching through `ALLUSERS`.
- No license agreement page or placeholder license is authored before the
  owner selects a project license.

## Installer lifecycle

Installing and uninstalling changes Windows Installer state, so perform this
check manually rather than as part of the automated build:

1. Record whether ztermy already appears under **Settings > Apps > Installed
   apps**.
2. Double-click the generated MSI and complete the per-user installation.
3. Launch `ztermy` from the Start menu and create a disposable host profile
   without saving a password or passphrase.
4. Close ztermy and run the same or a newer MSI again to exercise upgrade.
5. Launch from the Start menu once more and confirm the disposable profile is
   still present.
6. Uninstall ztermy from **Installed apps**.
7. Check the Start menu, installation directory, and the data directories
   reported by the last installed-mode log.

Expected:

- Installation and upgrade do not request administrator elevation.
- The Start menu shortcut launches the static executable without Qt, OpenSSL,
  Visual Studio, or a developer shell on `PATH`.
- Installed mode is reported in the log; no sibling `portable.flag` or
  portable `data` directory exists beside the executable.
- Upgrade preserves profiles, known-host trust, settings, logs, and crash
  diagnostics.
- Uninstall removes the executable, installation directory, Start menu
  shortcut, and Installed Apps entry.
- Uninstall deliberately preserves per-user application data so reinstalling
  does not destroy profiles or host trust.
- No runtime assertion, orphaned ztermy process, or PowerShell error dialog
  appears during install, launch, upgrade, or uninstall.

## Installed and isolated modes

Run the developer build normally, then with an isolated root:

```powershell
.\ztermy.exe
.\ztermy.exe --data-dir D:\tmp\ztermy-isolated
```

Expected:

- Normal mode logs `storageMode= installed` and preserves existing per-user
  profiles and trust data.
- Isolated mode logs `storageMode= custom` and writes only below the requested
  root.
- `--data-dir` without a path fails before the main window opens.
- Two conflicting `--data-dir` values are rejected.
