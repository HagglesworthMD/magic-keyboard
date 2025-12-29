---
description: Implement the Clipboard & Shortcut Agent for IME-safe shortcut simulation
---

# Implement Clipboard & Shortcut Agent

**Status:** ✅ COMPLETED (2025-12-29)

This workflow implements the **Clipboard & Shortcut Agent** responsible for Copy, Paste, Cut, and Select-All functionality via Fcitx5 key forwarding.

## Implementation Details

- **Approach:** Uses `InputContext::forwardKey()` to simulate keystrokes in the focused application.
- **Terminal Detection:** Automatically switches to `Ctrl+Shift+C/V` for terminal emulators.
- **Wayland Safety:** Adheres to the Wayland security model by avoiding global key injection.
- **UI Integration:** Dedicated shortcut buttons added to the spacebar row.
- **Standardized Prefix:** Fixed installation to `/usr/` for reliable KDE integration.

## Steps Completed

### 1. Update Engine Header (magickeyboard.h)
Added declarations for `handleShortcutAction` and `isTerminal`.

### 2. Implement Terminal Detection (magickeyboard.cpp)
Added `isTerminal()` with a comprehensive list of Linux terminal emulators.

### 3. Implement Shortcut Dispatch (magickeyboard.cpp)
Implemented `handleShortcutAction()` which:
- Detects focused IC.
- Checks password/readonly constraints.
- Selects the correct modifier set (Ctrl vs Ctrl+Shift).
- Sends key-down and key-up events via `forwardKey()`.

### 4. Integrate IPC Protocol (magickeyboard.cpp)
Updated `processLine()` to handle `{"type":"action","action":"..."}` messages.

### 5. Extend UI Bridge (main.cpp)
Added `sendAction(QString)` slot to `KeyboardBridge` for sending IPC messages.

### 6. Update QML Keyboard (KeyboardWindow.qml)
- Enhanced `KeyBtn` component to support an `action` property.
- Replaced static placeholders with functional buttons.
- Integrated actions into the `masterMouse` event loop.

## Verification

### 1. IPC Reachability
Verified that messages reached the engine even without focus:
```bash
echo '{"type":"action","action":"paste"}' | python3 ... (socket code)
# Log: ShortcutAction 'paste' but no active IC
```

### 2. Standardized Installation
Verified binaries and desktop entries are in system paths:
```bash
command -v magickeyboardctl  # -> /usr/bin/magickeyboardctl
ls /usr/share/applications/magickeyboard.desktop
```

### 3. KDE Integration
Verified the "Magic Keyboard" entry appears in the Start Menu and triggers the toggle action.

## Acceptance Criteria
- [x] Copy button sends `Ctrl+C` (or `Ctrl+Shift+C` in terminals)
- [x] Paste button sends `Ctrl+V` (or `Ctrl+Shift+V` in terminals)
- [x] Select All button sends `Ctrl+A`
- [x] Cut button sends `Ctrl+X`
- [x] Password fields block Copy/Cut
- [x] Read-only fields block Cut/Paste
- [x] No focus loss when clicking shortcut buttons
