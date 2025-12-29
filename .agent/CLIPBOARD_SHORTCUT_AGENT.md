# Clipboard & Shortcut Agent

**Magic Keyboard - Critical Subsystem Design Document**

## Executive Summary

This document defines the **Clipboard & Shortcut Agent** responsible for implementing Copy, Paste, Cut, and Select-All key handling for the Magic Keyboard. The agent uses **IME-safe shortcut simulation via Fcitx5**, ensuring correct modifier timing and compatibility with terminals, Wayland, and various application types.

---

## 1. Role & Mission Statement

The Clipboard & Shortcut Agent guarantees:

1. **Shortcut Simulation via Fcitx5**: All shortcut keys are forwarded through the sanctioned IM protocol
2. **Terminal Compatibility**: Detect terminals and use Ctrl+Shift+C/V instead of Ctrl+C/V
3. **Wayland-Safe Execution**: No global key injection, no bypassing compositor security
4. **Modifier Timing Correctness**: Proper key-down/key-up sequencing for modifier combinations
5. **No Focus Stealing**: Actions commit without disturbing the active application's focus

---

## 2. Architectural Constraints

### 2.1 What We MUST Do
- ✅ Route all shortcuts through `InputContext::forwardKey()` 
- ✅ Use proper key-down + key-up sequences for each key
- ✅ Detect terminal emulators for Ctrl+Shift shortcuts
- ✅ Respect application capability flags

### 2.2 What We MUST NOT Do
- ❌ **Direct clipboard injection** (write to wl-copy/xclip directly) — violates principle
- ❌ **Global key injection** via xdotool/ydotool/wtype — breaks Wayland security
- ❌ **Synthesizing keys at compositor level** — not available to IM frameworks
- ❌ **Focus manipulation** — keyboard never steals focus

### 2.3 Explicit Rejection: Why Not Direct Clipboard Access?

| Approach | Why Rejected |
|----------|--------------|
| `wl-paste` / `wl-copy` pipe | Bypasses application's clipboard handling; doesn't trigger paste action in text field |
| `xclip` / `xsel` (X11) | Doesn't work on Wayland; application still needs paste event |
| `QClipboard::mimeData()` read | Only reads clipboard; can't trigger paste into focused app |
| Creating synthetic key via wtype | Requires compositor/portal permissions; not available to input methods |

**The correct approach**: Send the keystroke that triggers the application's own copy/paste handler through the IM forward-key mechanism.

---

## 3. Architecture Overview

```
┌────────────────────────────────────────────────────────────────────────┐
│                       User Clicks Shortcut Key                          │
│              (Copy / Paste / Cut / Select All button)                   │
└─────────────────────────────────┬──────────────────────────────────────┘
                                  │
                                  ▼
┌────────────────────────────────────────────────────────────────────────┐
│                     magickeyboard-ui (Qt6/QML)                          │
│                                                                         │
│   Sends IPC message:                                                    │
│   {"type":"action","action":"paste"}                                    │
└─────────────────────────────────┬──────────────────────────────────────┘
                                  │
                                  ▼
┌────────────────────────────────────────────────────────────────────────┐
│                    Unix Domain Socket                                   │
└─────────────────────────────────┬──────────────────────────────────────┘
                                  │
                                  ▼
┌────────────────────────────────────────────────────────────────────────┐
│                  magickeyboard-engine (Fcitx5)                          │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │            ShortcutDispatcher (This Agent's Domain)               │  │
│  │                                                                   │  │
│  │  1. Determine active InputContext                                 │  │
│  │  2. Detect if terminal (check program())                          │  │
│  │  3. Select modifier set:                                          │  │
│  │     • Normal apps: Ctrl+C/V/X/A                                   │  │
│  │     • Terminals:   Ctrl+Shift+C/V (copy/paste only)               │  │
│  │  4. Forward key combination via ic->forwardKey()                  │  │
│  │                                                                   │  │
│  └───────────────────────────┬──────────────────────────────────────┘  │
│                              │                                          │
│                              ▼                                          │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │              InputContext::forwardKey()                           │  │
│  │                                                                   │  │
│  │  Sends key event via Wayland text-input / X11 XIM                 │  │
│  │  to the focused application                                       │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────┬──────────────────────────────────────┘
                                  │
                                  ▼
┌────────────────────────────────────────────────────────────────────────┐
│                   Target Application                                    │
│              (Kate, Firefox, Konsole, VS Code, etc.)                    │
│                                                                         │
│  Receives: KeyPress(Ctrl+V) / KeyPress(Ctrl+Shift+V)                    │
│  Action:   Application pastes from clipboard                            │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Shortcut Dispatch Strategy

### 4.1 Action-to-Key Mapping

| Action | Standard Shortcut | Terminal Shortcut | Notes |
|--------|-------------------|-------------------|-------|
| `copy` | Ctrl+C | Ctrl+Shift+C | Terminal uses Ctrl+C for SIGINT |
| `paste` | Ctrl+V | Ctrl+Shift+V | Terminal distinction required |
| `cut` | Ctrl+X | Ctrl+X | Same in terminals (less common) |
| `selectall` | Ctrl+A | Ctrl+A | Same in terminals |

### 4.2 Terminal Detection

```cpp
/**
 * Check if the current InputContext belongs to a terminal emulator.
 * Terminals require Ctrl+Shift+C/V instead of Ctrl+C/V for copy/paste.
 */
bool isTerminal(const std::string& program) {
    static const std::vector<std::string> terminals = {
        "konsole",
        "gnome-terminal",
        "alacritty", 
        "kitty",
        "foot",
        "xterm",
        "terminator",
        "tilix",
        "terminology",
        "wezterm",
        "hyper",
        "st",           // suckless terminal
        "urxvt",
        "mlterm",
        "sakura",
        "termite",
        "cool-retro-term",
        "yakuake",
        "guake",
        "tilda",
        "qterminal"
    };
    
    // Case-insensitive substring match
    std::string prog_lower = program;
    std::transform(prog_lower.begin(), prog_lower.end(), 
                   prog_lower.begin(), ::tolower);
    
    for (const auto& t : terminals) {
        if (prog_lower.find(t) != std::string::npos) {
            return true;
        }
    }
    return false;
}
```

### 4.3 Editor Detection (Optional - v0.4+)

Some editors have unique clipboard behaviors:

```cpp
/**
 * Editors that may need special handling (future use).
 * Currently treated as normal applications.
 */
bool isSpecialEditor(const std::string& program) {
    static const std::vector<std::string> editors = {
        "vim", "nvim", "neovim",    // Has own clipboard system
        "emacs",                      // Has own kill-ring
        "nano",                       // Ctrl+K/U for cut/paste
    };
    
    // MVP: Return false; treat as normal apps
    // These editors in terminal will get Ctrl+Shift+C/V via terminal detection
    return false;
}
```

---

## 5. Key Forwarding Implementation

### 5.1 Core Dispatch Function

```cpp
/**
 * Handle clipboard action request from UI.
 * Routes through InputContext::forwardKey() for Wayland safety.
 * 
 * @param action One of: "copy", "paste", "cut", "selectall"
 */
void MagicKeyboardEngine::handleShortcutAction(const std::string& action) {
    auto* ic = currentIC_;
    if (!ic) {
        MKLOG(Warn) << "ShortcutAction '" << action << "' but no active IC";
        return;
    }
    
    // Determine if we need terminal-mode shortcuts
    std::string program = ic->program();
    bool useShift = isTerminal(program) && 
                    (action == "copy" || action == "paste");
    
    // Map action to key symbol
    fcitx::KeySym sym = FcitxKey_None;
    
    if (action == "copy") {
        sym = FcitxKey_c;
    } else if (action == "paste") {
        sym = FcitxKey_v;
    } else if (action == "cut") {
        sym = FcitxKey_x;
    } else if (action == "selectall") {
        sym = FcitxKey_a;
    } else {
        MKLOG(Warn) << "Unknown shortcut action: " << action;
        return;
    }
    
    // Build key states
    fcitx::KeyStates states = fcitx::KeyState::Ctrl;
    if (useShift) {
        states |= fcitx::KeyState::Shift;
    }
    
    // Create key with modifiers
    fcitx::Key key(sym, states);
    
    // Forward key-down then key-up
    ic->forwardKey(key, /* isRelease */ false);
    ic->forwardKey(key, /* isRelease */ true);
    
    MKLOG(Info) << "Shortcut: " << action 
                << " -> " << (useShift ? "Ctrl+Shift+" : "Ctrl+")
                << static_cast<char>(sym - FcitxKey_a + 'A')
                << " program=" << program;
}
```

### 5.2 Modifier Timing Correctness

The `forwardKey()` call sequence is critical:

```
CORRECT:                          INCORRECT:
┌─────────────────────────┐      ┌─────────────────────────┐
│ KeyPress(Ctrl+V)        │      │ KeyPress(Ctrl)          │
│ KeyRelease(Ctrl+V)      │      │ KeyPress(V)             │
└─────────────────────────┘      │ KeyRelease(V)           │
                                 │ KeyRelease(Ctrl)        │
                                 └─────────────────────────┘
```

Fcitx5's `forwardKey()` with the correct `fcitx::Key` object handles this:
- The key symbol is `c`/`v`/`x`/`a`
- The states (`Ctrl`, optionally `Shift`) are bundled with the key
- `isRelease=false` sends key-down with modifiers
- `isRelease=true` sends key-up with modifiers

Applications expect to receive the keystroke as a combined event, not separate modifier presses.

### 5.3 Integration with Message Processing

```cpp
void MagicKeyboardEngine::processLine(const std::string& line) {
    // ... existing key handling ...
    
    // Handle action messages
    if (line.find("\"type\":\"action\"") != std::string::npos) {
        auto pos = line.find("\"action\":\"");
        if (pos != std::string::npos) {
            pos += 10;  // Length of "action":"
            auto end = line.find("\"", pos);
            if (end != std::string::npos) {
                std::string action = line.substr(pos, end - pos);
                handleShortcutAction(action);
            }
        }
        return;
    }
    
    // ... rest of existing processing ...
}
```

---

## 6. UI Integration

### 6.1 Keyboard Layout Keys

The QML keyboard should include dedicated shortcut keys. Recommended placement:

```
┌─────────────────────────────────────────────────────────────────┐
│ [Sel All]  [Cut]  [Copy]  [Paste]  │  ... number row ...        │
├─────────────────────────────────────────────────────────────────┤
│   Q    W    E    R    T    Y    U    I    O    P                │
│     A    S    D    F    G    H    J    K    L                   │
│  ⇧    Z    X    C    V    B    N    M    ⌫                      │
│ 123  🌐    ␣ (spacebar)         .    ⏎                          │
└─────────────────────────────────────────────────────────────────┘
```

Or in a top control row:

```
┌─────────────────────────────────────────────────────────────────┐
│          [ ⊞ Select All ]  [ ✂ Cut ]  [ ⎘ Copy ]  [ ⎗ Paste ]   │
├─────────────────────────────────────────────────────────────────┤
```

### 6.2 QML Key Definition

```qml
// In KeyboardWindow.qml - Shortcut keys section

Row {
    id: shortcutRow
    spacing: 4
    anchors.horizontalCenter: parent.horizontalCenter
    
    KeyButton {
        text: "Select All"
        icon: "select-all"
        onClicked: bridge.sendAction("selectall")
    }
    
    KeyButton {
        text: "Cut"
        icon: "edit-cut"
        onClicked: bridge.sendAction("cut")
    }
    
    KeyButton {
        text: "Copy"
        icon: "edit-copy"
        onClicked: bridge.sendAction("copy")
    }
    
    KeyButton {
        text: "Paste"
        icon: "edit-paste"
        onClicked: bridge.sendAction("paste")
    }
}
```

### 6.3 Bridge Extension

```cpp
// In main.cpp KeyboardBridge class

public slots:
    void sendAction(const QString& action) {
        if (socket_->state() != QLocalSocket::ConnectedState)
            return;
        QString msg = QString("{\"type\":\"action\",\"action\":\"%1\"}\n")
                          .arg(action);
        socket_->write(msg.toUtf8());
        socket_->flush();
        qDebug() << "Action sent:" << action;
    }
```

---

## 7. Edge Case Handling

### 7.1 Edge Case Matrix

| Scenario | Detection | Handling |
|----------|-----------|----------|
| **Terminal (Konsole, etc.)** | `program()` contains terminal name | Use Ctrl+Shift+C/V for copy/paste |
| **Password field** | `CapabilityFlag::Password` | Allow paste (Ctrl+V), block copy/cut |
| **Read-only field** | `CapabilityFlag::ReadOnly` | Allow copy, block cut/paste |
| **Browser text field** | `program()` = firefox/chromium | Normal Ctrl+C/V |
| **Browser URL bar** | Same as browser | Normal Ctrl+C/V |
| **Electron app (VS Code)** | `program()` contains relevant name | Normal Ctrl+C/V |
| **No focused IC** | `currentIC_ == nullptr` | Log warning, no action |
| **IM not active** | `shouldShowKeyboard()` returns false | Action still works (forward anyway) |

### 7.2 Password Field Handling

```cpp
void MagicKeyboardEngine::handleShortcutAction(const std::string& action) {
    auto* ic = currentIC_;
    if (!ic) {
        MKLOG(Warn) << "ShortcutAction no IC";
        return;
    }
    
    // Check capability restrictions
    auto caps = ic->capabilityFlags();
    
    // Password fields: allow paste, block copy/cut for security
    if (caps.test(fcitx::CapabilityFlag::Password)) {
        if (action == "copy" || action == "cut") {
            MKLOG(Debug) << "Blocked " << action << " in password field";
            return;
        }
    }
    
    // Read-only fields: allow copy/selectall, block cut/paste  
    if (caps.test(fcitx::CapabilityFlag::ReadOnly)) {
        if (action == "cut" || action == "paste") {
            MKLOG(Debug) << "Blocked " << action << " in readonly field";
            return;
        }
    }
    
    // ... proceed with normal handling ...
}
```

### 7.3 Terminal Copy/Paste Flow

```
User clicks [Copy] button in Magic Keyboard
                    │
                    ▼
        ┌───────────────────────┐
        │  Is program terminal? │
        └───────┬───────────────┘
                │
       ┌────────┴────────┐
       │ Yes             │ No
       ▼                 ▼
   Ctrl+Shift+C      Ctrl+C
       │                 │
       ▼                 ▼
   Konsole receives  Kate receives
   Ctrl+Shift+C      Ctrl+C
       │                 │
       ▼                 ▼
   Terminal copies   App copies
   selected text     selected text
```

---

## 8. Wayland Safety Verification

### 8.1 Why This Approach is Wayland-Safe

| Security Requirement | How We Comply |
|---------------------|---------------|
| No global key injection | We only send keys to the **focused InputContext** via `forwardKey()` |
| No reading other windows | We only operate on the IC that has focus |
| Uses sanctioned protocols | `forwardKey()` goes through Wayland text-input-v3 or X11 XIM |
| No compositor privileges | We don't request any special portal/compositor access |

### 8.2 Comparison with Rejected Approaches

| Approach | Security Risk | Our Approach |
|----------|---------------|--------------|
| `wtype` | Requires compositor permission | Use `forwardKey()` ✓ |
| `ydotool` | Creates virtual input device | Use `forwardKey()` ✓ |
| `wl-copy` | Only clipboard; no paste event | Use `forwardKey()` ✓ |
| `libinput` injection | Requires root/input group | Use `forwardKey()` ✓ |

### 8.3 Testing on Wayland vs X11

The implementation is identical for both:

```cpp
// This works on BOTH Wayland and X11:
ic->forwardKey(key, false);  // Key down
ic->forwardKey(key, true);   // Key up

// Fcitx5 internally handles:
// - Wayland: text-input-v3 protocol
// - X11: XIM/XCB key events
```

---

## 9. Terminal Mode Toggle (Future Enhancement - v0.3)

For manual override when auto-detection fails:

### 9.1 User-Visible Toggle

```cpp
// Engine-side state
bool terminalModeOverride_ = false;  // User override
bool terminalModeAuto_ = true;       // Use auto-detection

bool shouldUseTerminalShortcuts(fcitx::InputContext* ic) {
    if (!terminalModeAuto_) {
        return terminalModeOverride_;
    }
    return isTerminal(ic->program());
}
```

### 9.2 IPC Messages for Toggle

```json
// UI → Engine: Set terminal mode
{"type":"config","key":"terminal_mode","value":"auto"}
{"type":"config","key":"terminal_mode","value":"on"}
{"type":"config","key":"terminal_mode","value":"off"}

// Engine → UI: Report current mode
{"type":"config_state","terminal_mode":"auto","detected":true}
```

### 9.3 UI Toggle Button

```qml
// Terminal mode toggle in settings panel
Switch {
    id: terminalModeSwitch
    text: "Terminal Mode"
    checked: bridge.terminalMode
    onToggled: bridge.setTerminalMode(checked)
}
```

---

## 10. Protocol Extension

### 10.1 Updated IPC Protocol (protocol.h)

```cpp
// Add to existing action namespace:
namespace action {
    // Existing
    constexpr std::string_view COPY = "copy";
    constexpr std::string_view PASTE = "paste";
    constexpr std::string_view CUT = "cut";
    constexpr std::string_view SELECT_ALL = "selectall";
    
    // Future: More shortcuts
    constexpr std::string_view UNDO = "undo";
    constexpr std::string_view REDO = "redo";
}
```

### 10.2 Message Examples

```json
// UI → Engine: Trigger paste
{"type":"action","action":"paste"}

// UI → Engine: Trigger copy
{"type":"action","action":"copy"}

// UI → Engine: Trigger select all
{"type":"action","action":"selectall"}

// UI → Engine: Trigger cut  
{"type":"action","action":"cut"}
```

---

## 11. Testing Matrix

### 11.1 Basic Functionality Tests

| Test | Steps | Expected Behavior |
|------|-------|-------------------|
| Paste in Kate | 1. Copy text externally 2. Focus Kate 3. Click Paste key | Text appears at cursor |
| Copy from Kate | 1. Select text in Kate 2. Click Copy key 3. Paste externally | Text matches selection |
| Cut in Kate | 1. Select text 2. Click Cut key 3. Paste | Text moved, original deleted |
| Select All in Kate | 1. Focus Kate with text 2. Click Select All | All text selected |

### 11.2 Terminal Tests

| Test | Steps | Expected Behavior |
|------|-------|-------------------|
| Copy in Konsole | 1. Select text 2. Click Copy key | Text copied (Ctrl+Shift+C sent) |
| Paste in Konsole | 1. Copy text externally 2. Click Paste key | Text pasted (Ctrl+Shift+V sent) |
| Verify not SIGINT | 1. Run `cat` in Konsole 2. Click Copy | No ^C appears, no SIGINT |

### 11.3 Browser Tests

| Test | Steps | Expected Behavior |
|------|-------|-------------------|
| Copy from Firefox | 1. Select text on page 2. Click Copy | Text copied |
| Paste in Firefox form | 1. Focus text input 2. Click Paste | Text pasted |
| URL bar paste | 1. Focus Firefox URL bar 2. Click Paste | URL pasted |

### 11.4 Edge Case Tests

| Test | Steps | Expected Behavior |
|------|-------|-------------------|
| Copy in password field | 1. Focus password input 2. Select 3. Click Copy | **Action blocked** (log message) |
| Paste in password field | 1. Focus password 2. Click Paste | Text pasted (allowed) |
| Paste in readonly field | 1. Focus readonly 2. Click Paste | **Action blocked** |
| Copy from readonly | 1. Focus readonly 2. Select 3. Click Copy | Text copied (allowed) |
| Action without focus | 1. No IC active 2. Click any action | Warning logged, no crash |

### 11.5 Wayland Safety Tests

| Test | Steps | Expected Behavior |
|------|-------|-------------------|
| Multi-window paste | 1. Copy in App A 2. Focus App B 3. Click Paste | Paste goes to App B, not A |
| Background app | 1. App A in background 2. App B focused 3. Click Paste | Paste goes to B only |
| Keyboard stays passive | 1. Perform any action | App keeps focus, keyboard doesn't activate |

---

## 12. Implementation Checklist

### Phase 1: Core Implementation (v0.3)

- [ ] Add `handleShortcutAction()` to `magickeyboard.cpp`
- [ ] Implement `isTerminal()` with common terminal list
- [ ] Parse `{"type":"action","action":"..."}` messages in `processLine()`
- [ ] Add password/readonly field checks
- [ ] Add logging for shortcut dispatch

### Phase 2: UI Integration (v0.3)

- [ ] Add `sendAction(QString)` slot to `KeyboardBridge`
- [ ] Create shortcut button row in QML
- [ ] Style shortcut keys with appropriate icons
- [ ] Ensure buttons don't steal focus

### Phase 3: Testing (v0.3)

- [ ] Test with Kate (Qt/KDE)
- [ ] Test with Firefox (browser)
- [ ] Test with Konsole (terminal)
- [ ] Test with VS Code (Electron)
- [ ] Test password field blocking
- [ ] Verify Wayland safety

### Phase 4: Polish (v0.4)

- [ ] Add terminal mode toggle UI
- [ ] Persist terminal mode preference
- [ ] Expand terminal detection list based on user feedback
- [ ] Add undo/redo actions

---

## 13. Key Outputs Summary

### 13.1 Shortcut Dispatch Strategy

1. Receive action from UI via IPC: `{"type":"action","action":"paste"}`
2. Get current InputContext
3. Check for password/readonly restrictions
4. Detect if terminal via `program()` check
5. Build key with appropriate modifiers (Ctrl or Ctrl+Shift)
6. Forward key-down and key-up via `ic->forwardKey()`

### 13.2 Edge Case Handling

| Application Type | Copy/Paste | Cut | Select All |
|-----------------|------------|-----|------------|
| Normal apps | Ctrl+C/V | Ctrl+X | Ctrl+A |
| Terminals | Ctrl+Shift+C/V | Ctrl+X | Ctrl+A |
| Password fields | Blocked/Allowed | Blocked | Allowed |
| Read-only fields | Allowed | Blocked | Allowed |

### 13.3 Wayland Safety

- All shortcuts use `InputContext::forwardKey()` (sanctioned IM protocol)
- No global key injection
- No compositor-level privileges required
- Works identically on X11 and Wayland

---

## 14. Future Enhancements

### 14.1 Undo/Redo Support (v0.4)

```cpp
// Additional shortcuts
if (action == "undo") {
    sym = FcitxKey_z;
    // Ctrl+Z (standard)
} else if (action == "redo") {
    sym = FcitxKey_y;  // or Ctrl+Shift+Z for some apps
    // May need app-specific handling
}
```

### 14.2 Custom Shortcut Remapping (v0.5)

```json
// User-configurable shortcuts
{
    "shortcuts": {
        "copy": {"key": "c", "modifiers": ["ctrl"]},
        "paste": {"key": "v", "modifiers": ["ctrl"]},
        "custom1": {"key": "F2", "modifiers": []}
    }
}
```

### 14.3 Macro Support (v0.6)

```json
// Sequence of actions
{
    "type": "macro",
    "name": "duplicate_line",
    "steps": [
        {"action": "home"},
        {"action": "selectall_line"},
        {"action": "copy"},
        {"action": "end"},
        {"action": "enter"},
        {"action": "paste"}
    ]
}
```

---

## Revision History

| Date | Change |
|------|--------|
| 2024-12-29 | Initial Clipboard & Shortcut Agent design document |

---

## Appendix A: Fcitx5 Key Symbol Reference

```cpp
// Common key symbols used in shortcuts
FcitxKey_a = 0x61  // 'a' key
FcitxKey_c = 0x63  // 'c' key  
FcitxKey_v = 0x76  // 'v' key
FcitxKey_x = 0x78  // 'x' key
FcitxKey_z = 0x7a  // 'z' key

// Key states (modifiers)
fcitx::KeyState::Ctrl     // Control key
fcitx::KeyState::Shift    // Shift key
fcitx::KeyState::Alt      // Alt key
fcitx::KeyState::Super    // Super/Meta key
```

## Appendix B: Complete Terminal List

```cpp
// Comprehensive terminal detection (as of 2024)
static const std::vector<std::string> terminals = {
    // KDE
    "konsole", "yakuake",
    // GNOME
    "gnome-terminal", "tilix", "guake",
    // Cross-platform
    "alacritty", "kitty", "wezterm", "foot",
    // Traditional
    "xterm", "urxvt", "rxvt", "mlterm",
    // Lightweight
    "st", "sakura", "termite", "terminology",
    // Retro/Special
    "cool-retro-term",
    // Drop-down
    "tilda", "guake", "yakuake",
    // Qt-based
    "qterminal",
    // Electron-based (if detected as terminal)
    "hyper",
    // Any containing "terminal" or "term"
    // (handled by substring match)
};
```
