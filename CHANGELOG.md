# Changelog

## [Unreleased] - 2025-12-29
### Changed
- **Stability**: Added sender-side (engine) and receiver-side (UI) rate limiting for `ui_toggle` messages (100ms debounce).
- **Stability**: Cleaned up `QT_QPA_PLATFORM` environment variable scoping (now scoped to service).
- **Quality**: Added smoke tests for rapid toggle and restart resilience.
- **Code**: Refactored engine message handling to avoid `goto`.
