# Changelog

All notable changes will be documented in this file.

The project has not published a release.

## Unreleased

### Added

- Initial Qt Quick application shell and dark terminal-oriented visual baseline
- Windows 11 custom title bar with native non-client hit testing
- Snap Layout-compatible maximize-button hit region
- Native resize edges, work-area maximize constraints, DWM dark mode, rounded
  corners, and system backdrop integration
- Unit tests for window hit-test classification
- Dynamic and static Qt CMake build targets
- Categorized rotating file logs with Debug-build diagnostics

### Fixed

- Preserve native maximize capability when the window is created, execute
  maximize/restore from custom non-client button messages, and expose the
  custom caption-button bounds to Windows
