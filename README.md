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
sudo apt install build-essential cmake ninja-build pkg-config

# Fcitx5 development
sudo apt install fcitx5 fcitx5-config-qt libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev

# Qt6 development + QML runtime modules
sudo apt install qt6-base-dev qt6-declarative-dev \
    qml6-module-qtquick qml6-module-qtquick-controls \
    qml6-module-qtquick-layouts qml6-module-qtquick-window \
    qml6-module-qtqml-workerscript libxkbcommon-dev
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

## Testing (Development)

### 1. Install the addon

After building, install the Fcitx5 addon to system paths:

```bash
# Install the addon library
sudo cp build/lib/libmagickeyboard.so /usr/lib/x86_64-linux-gnu/fcitx5/

# Install the addon config (note: must be named magickeyboard.conf)
sudo cp build/src/engine/addon-magickeyboard.conf /usr/share/fcitx5/addon/magickeyboard.conf
```

### 2. Restart Fcitx5 and verify addon loads

```bash
pkill fcitx5
fcitx5 -d 2>&1 | grep -i magic
```

You should see:
```
Magic Keyboard engine initializing
Socket server listening at: /run/user/1000/magic-keyboard.sock
Loaded addon magickeyboard
Found 1 input method(s) in addon magickeyboard
```

### 3. Run the UI manually

```bash
./build/bin/magickeyboard-ui
```

You should see:
```
Magic Keyboard UI starting
Connected to engine
Window ready, hidden until activation
```

### 4. Install the binaries and systemd services

```bash
# Install binaries
sudo cp build/lib/libmagickeyboard.so /usr/lib/x86_64-linux-gnu/fcitx5/
sudo cp build/bin/magickeyboard-ui /usr/local/bin/

# Install user services
mkdir -p ~/.config/systemd/user
cp packaging/systemd/*.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable fcitx5
systemctl --user enable magickeyboard-ui
```

### 5. Configure Fcitx5 to use Magic Keyboard

Run `fcitx5-configtool` and add "Magic Keyboard" to your input method list.

### 6. Set up environment (Plasma session)

Create `~/.config/plasma-workspace/env/fcitx.sh`:

```bash
#!/bin/sh
export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx
```

#### For Flatpak apps (Firefox, etc.)

Run these to ensure IM works in sandboxed apps:
```bash
flatpak override --user --env=GTK_IM_MODULE=fcitx
flatpak override --user --env=QT_IM_MODULE=fcitx
flatpak override --user --env=XMODIFIERS=@im=fcitx
```

### 7. Test the full flow


**Must test in Wayland session** (check with `echo $XDG_SESSION_TYPE`):

1. Open Kate → click into text buffer → keyboard shows
2. Click sidebar (non-text) → keyboard hides
3. Click back into text → keyboard shows again
4. Open Firefox → URL bar → keyboard shows
5. Password field → keyboard should NOT show
6. Throughout: **Kate/Firefox never loses focus to keyboard**

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
