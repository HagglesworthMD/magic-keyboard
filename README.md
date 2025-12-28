# Magic Keyboard

**SteamOS Desktop Mode On-Screen Keyboard for KDE Plasma**

A proper Input Method implementation using Fcitx5, optimized for Apple Magic Trackpad with swipe typing support.

## Features (Planned)

- ✅ Auto-shows on text field focus, auto-hides on blur
- ✅ Works on Wayland + X11 (respects Wayland security model)
- ✅ Works across KDE apps, browsers, Electron, terminals
- ✅ Swipe typing via pointer click-and-drag
- ✅ Never steals focus from target app
- ✅ Copy/Paste/Cut/Select All keys
- ✅ Update-safe deployment on Steam Deck

## Architecture

This is a **proper Fcitx5 Input Method**, not an overlay hack.

- **Engine**: Fcitx5 addon that owns composition and commits text
- **UI**: Separate Qt6/QML process that renders keyboard, never takes focus
- **IPC**: Unix domain socket for high-rate events, D-Bus for control signals only

See [ARCHITECTURE.md](ARCHITECTURE.md) for full technical details.

## Build Requirements

### Ubuntu 24.04 (Development)

```bash
# Core build tools
sudo apt install build-essential cmake ninja-build

# Fcitx5 development
sudo apt install fcitx5 libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev

# Qt6 development
sudo apt install qt6-base-dev qt6-declarative-dev qml6-module-qtquick

# Optional: for testing
sudo apt install fcitx5-frontend-qt6 fcitx5-frontend-gtk4
```

### SteamOS / Arch Linux (Target)

```bash
# Enable developer mode and unlock filesystem first
sudo steamos-readonly disable  # SteamOS only

# Install dependencies
sudo pacman -S fcitx5 fcitx5-qt qt6-base qt6-declarative cmake ninja
```

## Building

```bash
# Configure
cmake -S . -B build -G Ninja

# Build
cmake --build build

# Install (to user prefix for development)
cmake --install build --prefix ~/.local
```

## Running

1. Ensure Fcitx5 is running:
   ```bash
   fcitx5 -d
   ```

2. Add Magic Keyboard as an input method via `fcitx5-configtool`

3. The keyboard UI will auto-launch when you focus a text field

## Project Status

| Version | Status | Features |
|---------|--------|----------|
| v0.1    | 🚧 In Progress | Click-to-type, show/hide on focus |
| v0.2    | Planned | Swipe typing, candidate selection |
| v0.3    | Planned | Copy/paste, settings, SteamOS packaging |

## License

TBD

## Contributing

See [ARCHITECTURE.md](ARCHITECTURE.md) for technical constraints before contributing.
