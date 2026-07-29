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
