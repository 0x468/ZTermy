# ADR 0009: Installed and portable storage modes

Status: accepted

## Context

ztermy needs predictable per-user storage when installed and a genuinely
self-contained mode for a removable or unpacked portable build. Profiles,
known-host trust, settings, logs, and crash dumps must agree on the selected
mode. A portable package must never silently write into the user's installed
application data directories.

## Decision

Application paths are resolved once during startup and passed explicitly to
the logging, crash-diagnostics, profile, and SSH trust boundaries.

Installed mode uses Qt's Windows application data locations:

- roaming application data: `profiles.json`, `known_hosts.json`, and
  `settings.json`;
- local application data: `logs` and `crashes`.

Portable mode is enabled by `--portable` or a `portable.flag` file beside
`ztermy.exe`. All data then lives below a `data` directory beside the
executable. The static portable package contains the marker.

`--data-dir <path>` selects an explicit isolated root. It is primarily intended
for testing and advanced local deployments, and takes precedence over portable
mode.

Startup fails before opening the main window if required directories cannot be
created. Paths may be logged, but credentials, passphrases, private-key
contents, terminal input, and clipboard contents remain forbidden.

## Consequences

- A storage mode cannot split host profiles and known-host trust across
  unrelated roots.
- Portable archives can be moved as a unit and leave no application data
  outside their directory.
- An installed build must not ship `portable.flag`, especially under
  `Program Files`, where the directory is normally not writable.
- Future schema migrations have one well-defined data root per mode.
- Portable and custom-data modes keep their encrypted credential vault at
  `credentials.zvlt` below that same root; installed mode uses the operating
  system vault by default.
- The current portable package is a static-Qt ZIP. An installer remains a
  separate artifact and deliberately excludes the portable marker.
