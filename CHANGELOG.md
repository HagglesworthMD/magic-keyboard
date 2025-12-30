# Changelog

## [Unreleased] - 2025-12-29
### Changed
- **Stability**: Added sender-side (engine) and receiver-side (UI) rate limiting for `ui_toggle` messages (100ms debounce).
- **Stability**: Cleaned up `QT_QPA_PLATFORM` environment variable scoping (now scoped to service).
- **Quality**: Added smoke tests for rapid toggle and restart resilience.
- **Code**: Refactored engine message handling to avoid `goto`.
- **Stability**: Implemented end-to-end sequence tagging for swipe gestures to ensure robust latency attribution and stale candidate prevention.
- **Operations**: Added XDG-aware data resolution in the engine, prioritizing `~/.local/share` for layouts and dictionaries.
- **UI**: Added a manual "Hide/Show" toggle button to the keyboard UI as an emergency override.
- **Hardening**: Sanitize and validate Unix socket paths in the UI connection logic.
- **Deployment**: Improved CMake installation to include dictionary data files.
- **Deployment**: Added a sample `fcitx5.service` user systemd unit for easier service management.
- **Packaging**: Fixed broken desktop entries and systemd unit paths by dynamically configuring them with CMake install prefix. Added "Toggle", "Show", and "Hide" actions to app launcher.
