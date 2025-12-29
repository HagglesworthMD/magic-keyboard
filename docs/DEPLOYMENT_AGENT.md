# Packaging & SteamOS Deployment Agent

**Authoritative Technical Design Document**

This document defines the deployment strategy for Magic Keyboard on SteamOS. The Deployment Agent is responsible for ensuring the keyboard survives OS updates, integrates cleanly with Fcitx5 and KDE Plasma, and remains user-installable without breaking the system.

---

## Role & Authority

**The Deployment Agent owns all installation and lifecycle decisions.**

- Flatpak vs system install trade-offs
- Fcitx5 integration requirements
- Update resilience mechanisms
- Logging and diagnostics strategy
- systemd user services configuration

**Explicitly Separated From:**
- Runtime behavior → handled by Engine/UI Agents
- IPC protocol → handled by IPC Agent
- Gesture detection → handled by Gesture Agent
- Text composition → handled by Fcitx5 core

---

## Hard Constraints

### 1. SteamOS Immutable Root Filesystem

SteamOS uses an immutable A/B partition scheme. The root filesystem (`/usr`) is:
- Read-only by default
- Wiped and replaced on every SteamOS update
- Never the place for user-installed software

**Implications:**
| Location | Survives Update? | Safe for Install? |
|----------|------------------|-------------------|
| `/usr/` | ❌ No | ❌ Forbidden |
| `/usr/local/` | ✅ Yes | ✅ Recommended |
| `/opt/` | ✅ Yes | ✅ Alternative |
| `~/.local/` | ✅ Yes | ✅ User-space |
| `/home/` | ✅ Yes | ✅ User data |

**Rule:** NEVER install to `/usr/`. ALWAYS prefer `/usr/local/` or `~/.local/`.

### 2. Fcitx5 Session Integration

Fcitx5 is not just a library—it's a **session service** that must:
- Start before applications launch
- Provide environment variables to child processes
- Register with the compositor for input protocol

**Critical Environment Variables:**
```bash
GTK_IM_MODULE=fcitx
QT_IM_MODULE=fcitx
XMODIFIERS=@im=fcitx
```

These MUST be set in the user's session BEFORE any GUI apps start.

### 3. Flatpak Sandbox Incompatibility

**Flatpak for the IM engine is FORBIDDEN.**

| Requirement | Flatpak Compatibility |
|-------------|----------------------|
| D-Bus session access | ⚠️ Limited (portal) |
| Unix socket IPC | ❌ Sandbox boundary |
| Application focus signals | ❌ Cannot observe |
| Fcitx5 addon loading | ❌ Wrong library path |
| Session-level env vars | ❌ Per-app only |

The Magic Keyboard engine MUST run as a native host application.

### 4. KDE Plasma Wayland

The keyboard UI must work correctly on:
- KDE Plasma 5.x (SteamOS 3.4)
- KDE Plasma 6.x (future SteamOS)
- Wayland session (default on Steam Deck)
- X11 session (fallback mode)

**No wlroots assumptions.** KDE uses kwin, not wlroots.

---

## Deployment Model Decision

### Summary

| Component | Install Location | Install Type |
|-----------|-----------------|--------------|
| `libmagickeyboard.so` | `/usr/local/lib/fcitx5/` | Native (pacman/manual) |
| `magickeyboard.conf` | `/usr/local/share/fcitx5/addon/` | Native |
| `magic-keyboard.conf` | `/usr/local/share/fcitx5/inputmethod/` | Native |
| `magickeyboard-ui` | `/usr/local/bin/` | Native |
| `magickeyboardctl` | `/usr/local/bin/` | Native |
| `layouts/`, `dictionaries/` | `/usr/local/share/magic-keyboard/` | Native |
| User service | `~/.config/systemd/user/` | User |
| Autostart fallback | `~/.config/autostart/` | User |
| Env vars | `~/.config/plasma-workspace/env/` | User |
| Fcitx5 user config | `~/.config/fcitx5/` | User |

### Why NOT Flatpak

Flatpak was evaluated and rejected:

1. **IM Sandbox Conflict**: Input methods need session-level access to coordinate with all applications. Flatpak's per-app sandbox model breaks this.

2. **Socket IPC**: Our Unix domain socket (`/run/user/{uid}/magic-keyboard.sock`) would need `--filesystem=xdg-run` permission and still face issues with socket paths.

3. **Fcitx5 Addon Loading**: Fcitx5 loads addons from hardcoded paths. A Flatpak-installed addon would need to modify Fcitx5's search path, which isn't possible without modifying Fcitx5 itself.

4. **Environment Variables**: IM environment variables must be set system-wide, not per-Flatpak-app.

### Why /usr/local/

`/usr/local/` is the blessed location for user-installed software on SteamOS:

- Survives OS updates (separate from immutable `/usr/`)
- Follows FHS conventions
- Fcitx5 already searches `/usr/local/lib/fcitx5/` for addons
- No need for `steamos-readonly disable` during normal operation

**Exception**: Initial installation requires `steamos-readonly disable` temporarily to create directories under `/usr/local/`. Once created, the directory persists.

---

## Fcitx5 Integration Details

### Addon Discovery

Fcitx5 searches for addons in these paths (in order):

```
$XDG_DATA_HOME/fcitx5/addon/           # ~/.local/share/fcitx5/addon/
$XDG_DATA_DIRS/fcitx5/addon/           # /usr/local/share/fcitx5/addon/, /usr/share/fcitx5/addon/
```

For **libraries**, Fcitx5 uses `dlopen()` with these search paths:

```
$FCITX_ADDON_DIR                       # Custom override
$CMAKE_INSTALL_PREFIX/lib/fcitx5/      # /usr/local/lib/fcitx5/
/usr/lib/fcitx5/                       # System default
```

### Required Files

```
/usr/local/lib/fcitx5/
└── libmagickeyboard.so                # The Fcitx5 addon binary

/usr/local/share/fcitx5/addon/
└── magickeyboard.conf                 # Addon metadata (Category, Library, etc.)

/usr/local/share/fcitx5/inputmethod/
└── magic-keyboard.conf                # Input method registration
```

### Addon Configuration Format

**`magickeyboard.conf`** (addon metadata):
```ini
[Addon]
Name=Magic Keyboard
Category=InputMethod
Library=libmagickeyboard
Configurable=True
Enabled=True

[Dependencies]
OptionalDependencies=
```

**`magic-keyboard.conf`** (input method):
```ini
[InputMethod]
Name=Magic Keyboard
Icon=input-keyboard
Label=MK
LangCode=en
Addon=magickeyboard
Configurable=True
```

### Session Environment Setup

Users must configure their Plasma session to export IM variables.

**Create `~/.config/plasma-workspace/env/fcitx5.sh`:**
```bash
#!/bin/sh
export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx
```

Make it executable:
```bash
chmod +x ~/.config/plasma-workspace/env/fcitx5.sh
```

**For Flatpak apps** (Firefox, etc.):
```bash
flatpak override --user --env=GTK_IM_MODULE=fcitx
flatpak override --user --env=QT_IM_MODULE=fcitx
flatpak override --user --env=XMODIFIERS=@im=fcitx
```

---

## Update Resilience Strategy

### Problem: What Gets Wiped?

| Event | `/usr/` | `/usr/local/` | `~/.local/` | `~/.config/` |
|-------|---------|---------------|-------------|--------------|
| SteamOS update | ❌ Wiped | ✅ Preserved | ✅ Preserved | ✅ Preserved |
| Factory reset | ❌ Wiped | ⚠️ Unclear | ❌ Wiped | ❌ Wiped |
| User profile switch | N/A | ✅ Preserved | ⚠️ Per-user | ⚠️ Per-user |

### Solution: Layered Installation

**Layer 1: Core binaries** (`/usr/local/`)
- Survives SteamOS updates
- Installed once, updated manually by user

**Layer 2: User configuration** (`~/.config/`, `~/.local/`)
- Survives SteamOS updates
- Per-user settings and services
- Can be backed up/restored

**Layer 3: Runtime state** (`$XDG_RUNTIME_DIR`)
- Ephemeral, recreated each session
- Sockets, PID files, etc.

### Verification After Update

The installer (PKGBUILD) should create a verification script:

**`/usr/local/bin/magic-keyboard-verify`:**
```bash
#!/bin/bash
set -e

echo "=== Magic Keyboard Installation Verification ==="

# Check addon
if [[ -f /usr/local/lib/fcitx5/libmagickeyboard.so ]]; then
    echo "✓ Addon library present"
else
    echo "✗ Addon library MISSING"
    exit 1
fi

# Check config files
if [[ -f /usr/local/share/fcitx5/addon/magickeyboard.conf ]]; then
    echo "✓ Addon config present"
else
    echo "✗ Addon config MISSING"
    exit 1
fi

# Check UI binary
if command -v magickeyboard-ui &>/dev/null; then
    echo "✓ UI binary found"
else
    echo "✗ UI binary MISSING"
    exit 1
fi

# Check Fcitx5 can load it
if fcitx5 -v 2>&1 | grep -q "magickeyboard"; then
    echo "✓ Fcitx5 recognizes addon"
else
    echo "⚠ Addon not loaded (may need session restart)"
fi

echo ""
echo "Installation looks healthy!"
```

---

## systemd User Services

### Architecture

```
graphical-session.target
        │
        ├── fcitx5.service (KDE-managed or manually created)
        │         │
        │         ▼ (socket) /run/user/{uid}/fcitx5
        │
        └── magickeyboard-ui.service (our service)
                  │
                  ▼ (socket) /run/user/{uid}/magic-keyboard.sock
```

### Service Definition

**`~/.config/systemd/user/magickeyboard-ui.service`:**
```ini
[Unit]
Description=Magic Keyboard UI
After=graphical-session.target
After=fcitx5.service
PartOf=graphical-session.target

# Ensure we restart if Fcitx5 restarts
BindsTo=fcitx5.service

[Service]
Type=simple
ExecStart=/usr/local/bin/magickeyboard-ui
Restart=on-failure
RestartSec=2
StartLimitIntervalSec=60
StartLimitBurst=5

# Wayland environment
Environment=QT_QPA_PLATFORM=wayland
Environment=QT_WAYLAND_DISABLE_WINDOWDECORATION=1

# Inherit session environment (for WAYLAND_DISPLAY, etc.)
PassEnvironment=WAYLAND_DISPLAY XDG_RUNTIME_DIR XDG_SESSION_TYPE

# Resource limits
MemoryMax=256M
CPUQuota=50%

[Install]
WantedBy=graphical-session.target
```

### Enabling the Service

```bash
# Reload systemd
systemctl --user daemon-reload

# Enable and start
systemctl --user enable --now magickeyboard-ui.service

# Check status
systemctl --user status magickeyboard-ui.service
```

### Fallback: KDE Autostart

If systemd user services prove unreliable (which they can be on SteamOS), use KDE Autostart instead:

**`~/.config/autostart/magickeyboard-ui.desktop`:**
```ini
[Desktop Entry]
Type=Application
Name=Magic Keyboard UI
Comment=On-screen keyboard for KDE Plasma
Exec=/usr/local/bin/magickeyboard-ui
Icon=input-keyboard
Categories=Utility;
X-KDE-autostart-phase=2
X-KDE-autostart-condition=fcitx5.desktop:always
```

**Recommendation**: Provide BOTH. Let users choose based on what works for their setup.

---

## Logging Strategy

### Log Destinations

| Component | Log Destination | View Command |
|-----------|-----------------|--------------|
| Fcitx5 daemon | `journalctl --user -u fcitx5` | Built-in |
| Magic Keyboard engine | `journalctl --user -u fcitx5` | As Fcitx5 addon |
| Magic Keyboard UI | `journalctl --user -u magickeyboard-ui` | Our service |

### Log Levels

```cpp
enum class LogLevel {
    Error,      // Critical failures only
    Warning,    // Recoverable issues
    Info,       // Lifecycle events (show/hide)
    Debug,      // IPC message flow
    Trace       // Every pointer event (VERY verbose)
};
```

**Default**: `Info` in production, `Debug` during development.

### Structured Logging Format

```cpp
// Format: [MagicKB] <component> <level>: <message>
qInfo() << "[MagicKB] UI Info: Window shown";
qWarning() << "[MagicKB] Engine Warning: Socket reconnecting";
qDebug() << "[MagicKB] IPC Debug: Received" << messageCount << "bytes";
```

Use `[MagicKB]` prefix for grep-ability:
```bash
journalctl --user | grep "\[MagicKB\]"
```

### Debug Mode Toggle

**Runtime**: Send `{"type":"set_log_level","level":"debug"}` via socket.

**Startup**: Set environment variable:
```bash
MAGICKEYBOARD_LOG_LEVEL=debug magickeyboard-ui
```

### Crash Reporting

When the UI crashes, systemd captures the failure:
```bash
journalctl --user -u magickeyboard-ui.service -p err
```

Consider writing last-known-state to a file before crash:
**`~/.local/state/magic-keyboard/last-state.json`:**
```json
{
  "timestamp": "2024-12-29T10:30:00Z",
  "visible": true,
  "focused_app": "kate",
  "last_key": "swipe_complete",
  "uptime_seconds": 3600
}
```

---

## Installation Procedure

### Prerequisites

```bash
# Enable write access to /usr/local (SteamOS only, one-time)
sudo steamos-readonly disable

# Ensure directories exist
sudo mkdir -p /usr/local/lib/fcitx5
sudo mkdir -p /usr/local/share/fcitx5/addon
sudo mkdir -p /usr/local/share/fcitx5/inputmethod
sudo mkdir -p /usr/local/share/magic-keyboard/layouts
sudo mkdir -p /usr/local/bin

# Re-enable read-only (optional, for safety)
sudo steamos-readonly enable
```

### From Source (Development)

```bash
# Build
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build

# Install (requires writable /usr/local)
sudo cmake --install build
```

### From Package (PKGBUILD)

```bash
# Option 1: Build and install locally
makepkg -si

# Option 2: Use AUR helper
yay -S magic-keyboard
```

### Post-Install Configuration

```bash
# 1. Set up session environment
mkdir -p ~/.config/plasma-workspace/env
cat > ~/.config/plasma-workspace/env/fcitx5.sh << 'EOF'
#!/bin/sh
export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx
EOF
chmod +x ~/.config/plasma-workspace/env/fcitx5.sh

# 2. Set up Flatpak overrides
flatpak override --user --env=GTK_IM_MODULE=fcitx
flatpak override --user --env=QT_IM_MODULE=fcitx
flatpak override --user --env=XMODIFIERS=@im=fcitx

# 3. Enable UI service
mkdir -p ~/.config/systemd/user
cp /usr/local/share/magic-keyboard/magickeyboard-ui.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable magickeyboard-ui.service

# 4. Add Magic Keyboard to Fcitx5 input methods
# (GUI: System Settings → Input Method → Add "Magic Keyboard")

# 5. Log out and back in (or reboot)
```

---

## PKGBUILD Enhancement

Updated PKGBUILD for update-safe installation:

**`packaging/arch/PKGBUILD`:**
```bash
# Maintainer: Your Name <your@email.com>
pkgname=magic-keyboard
pkgver=0.3.0
pkgrel=1
pkgdesc="SteamOS Desktop Mode On-Screen Keyboard for KDE Plasma"
arch=('x86_64')
url="https://github.com/HagglesworthMD/magic-keyboard"
license=('MIT')
depends=(
    'fcitx5'
    'fcitx5-qt'
    'qt6-base'
    'qt6-declarative'
)
makedepends=(
    'cmake'
    'ninja'
    'qt6-tools'
)
optdepends=(
    'fcitx5-configtool: GUI configuration'
)
source=("$pkgname-$pkgver.tar.gz")
sha256sums=('SKIP')
backup=(
    'usr/local/share/magic-keyboard/layouts/qwerty.json'
)

build() {
    cmake -S "$pkgname-$pkgver" -B build \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local
    cmake --build build
}

package() {
    DESTDIR="$pkgdir" cmake --install build
    
    # Install systemd user service template
    install -Dm644 "$srcdir/$pkgname-$pkgver/packaging/systemd/magickeyboard-ui.service" \
        "$pkgdir/usr/local/share/magic-keyboard/magickeyboard-ui.service"
    
    # Install verification script
    install -Dm755 "$srcdir/$pkgname-$pkgver/packaging/scripts/magic-keyboard-verify" \
        "$pkgdir/usr/local/bin/magic-keyboard-verify"
    
    # Install post-install instructions
    install -Dm644 "$srcdir/$pkgname-$pkgver/packaging/scripts/post-install.txt" \
        "$pkgdir/usr/local/share/doc/magic-keyboard/post-install.txt"
}

post_install() {
    echo ""
    echo "=== Magic Keyboard Installed ==="
    echo ""
    echo "IMPORTANT: Complete these steps to finish setup:"
    echo ""
    echo "1. Set up session environment:"
    echo "   cat /usr/local/share/doc/magic-keyboard/post-install.txt"
    echo ""
    echo "2. Enable the UI service:"
    echo "   systemctl --user enable magickeyboard-ui"
    echo ""
    echo "3. Add Magic Keyboard in Fcitx5 settings"
    echo ""
    echo "4. Log out and back in"
    echo ""
}
```

---

## Uninstallation Procedure

### Clean Removal

```bash
# Stop services
systemctl --user stop magickeyboard-ui.service
systemctl --user disable magickeyboard-ui.service

# Remove user config
rm -f ~/.config/systemd/user/magickeyboard-ui.service
rm -f ~/.config/autostart/magickeyboard-ui.desktop
rm -rf ~/.config/magic-keyboard/
rm -rf ~/.local/state/magic-keyboard/

# Remove system files (requires writable /usr/local)
sudo steamos-readonly disable
sudo rm -f /usr/local/lib/fcitx5/libmagickeyboard.so
sudo rm -f /usr/local/share/fcitx5/addon/magickeyboard.conf
sudo rm -f /usr/local/share/fcitx5/inputmethod/magic-keyboard.conf
sudo rm -rf /usr/local/share/magic-keyboard/
sudo rm -f /usr/local/bin/magickeyboard-ui
sudo rm -f /usr/local/bin/magickeyboardctl
sudo rm -f /usr/local/bin/magic-keyboard-verify
sudo steamos-readonly enable

# Restart Fcitx5
pkill fcitx5
# (Fcitx5 will restart automatically on next focus)
```

---

## Failure Modes & Mitigations

### Failure: UI Doesn't Start

| Symptom | Cause | Fix |
|---------|-------|-----|
| Service fails immediately | Missing Wayland socket | Check `$WAYLAND_DISPLAY` is set |
| Service loops | Binary crash | Check `journalctl --user -u magickeyboard-ui` |
| Service never starts | Wrong target | Ensure `graphical-session.target` is active |

### Failure: Keyboard Doesn't Show

| Symptom | Cause | Fix |
|---------|-------|-----|
| No show on text focus | IM module not set | Check `$QT_IM_MODULE=fcitx` |
| Works in Qt, not GTK | GTK module not set | Check `$GTK_IM_MODULE=fcitx` |
| Works in native, not Flatpak | Flatpak override missing | Run `flatpak override --user --env=...` |

### Failure: After SteamOS Update

| Symptom | Cause | Fix |
|---------|-------|-----|
| Binary missing | Installed to `/usr/` | Reinstall to `/usr/local/` |
| Works but no config | User config preserved | Just works, no action needed |
| Fcitx5 doesn't find addon | Library path wrong | Check `/usr/local/lib/fcitx5/` |

### Failure: Fcitx5 Crashes

| Symptom | Cause | Fix |
|---------|-------|-----|
| Fcitx5 segfaults on load | Addon ABI mismatch | Rebuild against current Fcitx5 |
| Fcitx5 ignores addon | Config format wrong | Check `magickeyboard.conf` syntax |

---

## Security Considerations

### Socket Permissions

The IPC socket must be user-only:
```cpp
// In engine startup
chmod("/run/user/{uid}/magic-keyboard.sock", 0600);
```

### No SUID/SGID

Magic Keyboard should NEVER run as root or with elevated privileges.

All files owned by user `deck` (UID 1000 on Steam Deck).

### No Network Access

Magic Keyboard is fully offline:
- No telemetry
- No cloud features
- No update checks
- Dictionary is local file

---

## Acceptance Criteria

### Installation

1. **Fresh Install**: User can install on fresh SteamOS following documented steps
2. **No Root Required**: After initial `/usr/local/` directory creation, no root access needed
3. **Self-Contained**: All dependencies available in standard SteamOS repos

### Update Resilience

1. **Survive Update**: Magic Keyboard works after SteamOS update without reinstallation
2. **Config Preserved**: User settings survive SteamOS update
3. **Verification**: `magic-keyboard-verify` confirms installation health

### Integration

1. **Session Auto-Start**: UI starts automatically on login to Desktop Mode
2. **Fcitx5 Discovery**: Addon appears in Fcitx5 configuration tools
3. **Wayland Compatible**: Full functionality on Wayland (default SteamOS session)

### Diagnostics

1. **Logs Available**: All components log to journald
2. **Grep-able**: Logs have `[MagicKB]` prefix
3. **Debug Mode**: Can enable verbose logging without restart

---

## Revision History

| Date | Change |
|------|--------|
| 2024-12-29 | Initial Deployment Agent design document |

---

## Related Documents

- [ARCHITECTURE.md](../ARCHITECTURE.md) - System architecture overview
- [GESTURE_AGENT.md](GESTURE_AGENT.md) - Gesture detection system
- [KEYBOARD_UI_AGENT.md](KEYBOARD_UI_AGENT.md) - UI layout and rendering
- [README.md](../README.md) - Quick start guide
