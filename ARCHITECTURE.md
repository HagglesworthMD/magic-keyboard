# Magic Keyboard Architecture

**Authoritative Technical Design Document**

This document defines the architectural constraints for Magic Keyboard. All implementation decisions must align with this spec.

---

## Hard Constraints (Non-Negotiable)

### Framework: Fcitx5

We are building this as a **Fcitx5 input method addon**. This is locked.

**Why Fcitx5:**
- Strongest Plasma/Wayland integration
- Correct input-context signals for show/hide
- Avoids Wayland-forbidden global key injection
- Modular addon architecture
- Best path to long-term maintainability

**Explicitly Rejected:**
- Overlay keyboards that inject keys via xdotool/ydotool
- Global key grabbing (breaks Wayland security model)
- Focus-stealing popup windows

### Wayland Security Model

- No global key injection
- No observation of compositor-private focus events
- All text input goes through sanctioned IM protocol
- UI uses standard window properties, not compositor hacks

### KDE Plasma Compatibility

- **Do NOT assume wlr-layer-shell exists** (that's wlroots-specific)
- Use standard Qt window flags for non-focusable tool windows
- Must work on both KDE Wayland and X11

---

## System Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                         Application                               │
│                    (Qt/GTK/Electron/Browser)                      │
└──────────────────────────────────────────────────────────────────┘
                                │
                                │ Wayland text-input / X11 XIM
                                ▼
┌──────────────────────────────────────────────────────────────────┐
│                        Fcitx5 Daemon                              │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │              magickeyboard-engine (addon)                   │  │
│  │                                                             │  │
│  │  • Tracks InputContext lifecycle                            │  │
│  │  • Maintains preedit + candidate list                       │  │
│  │  • Commits text to focused app                              │  │
│  │  • Handles shortcut actions (copy/paste)                    │  │
│  │  • Emits show/hide control messages                         │  │
│  └────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
                                │
                                │ Unix Domain Socket (high-rate)
                                │ D-Bus (control signals only)
                                ▼
┌──────────────────────────────────────────────────────────────────┐
│                    magickeyboard-ui (Qt6/QML)                     │
│                                                                   │
│  • Renders QWERTY keyboard + candidate bar                        │
│  • Receives pointer events (click, drag)                          │
│  • Sends key/swipe events to engine                               │
│  • NEVER takes focus from target app                              │
│                                                                   │
│  Window Flags:                                                    │
│    Qt::Tool | Qt::FramelessWindowHint |                           │
│    Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus        │
└──────────────────────────────────────────────────────────────────┘
```

---

## Module Breakdown

### 1. Fcitx5 Addon: `magickeyboard-engine`

**Location:** `src/engine/`

**Responsibilities:**
- Register as Fcitx5 InputMethodEngine
- Track current InputContext (focus in/out)
- Maintain composition state (preedit string, cursor position)
- Commit finalized text to application
- Handle copy/paste/cut/selectall actions via IM key forwarding
- Signal UI to show/hide based on context activation

**Key Interfaces:**
```cpp
class MagicKeyboardEngine : public fcitx::InputMethodEngineV2 {
    void activate(const InputMethodEntry&, InputContextEvent&) override;
    void deactivate(const InputMethodEntry&, InputContextEvent&) override;
    void keyEvent(const InputMethodEntry&, KeyEvent&) override;
    // ...
};
```

### 2. UI Process: `magickeyboard-ui`

**Location:** `src/ui/`

**Responsibilities:**
- Render keyboard layout (QML)
- Handle pointer input (press, move, release)
- Draw swipe trail visualization
- Show candidate bar
- Send events to engine via IPC
- Position at screen bottom, avoid text field overlap (future)

**Window Properties (CRITICAL):**
```cpp
window->setFlags(
    Qt::Tool |
    Qt::FramelessWindowHint |
    Qt::WindowStaysOnTopHint |
    Qt::WindowDoesNotAcceptFocus
);
// On X11: also set WM_HINTS InputHint=False, _NET_WM_WINDOW_TYPE_UTILITY
```

### 3. IPC Layer: `src/ipc/`

**Protocol Design:**

| Channel | Transport | Use Case |
|---------|-----------|----------|
| Control | D-Bus | Show, Hide, SetState, GetState |
| Events | Unix Socket | KeyPressed, SwipeUpdate, SwipeComplete |

**Why Unix Socket for Events:**
- D-Bus adds 0.5-2ms latency per message
- Swipe sampling at 120Hz = 8ms budget
- D-Bus overhead would consume 10-25% of budget
- Socket: single write() syscall, ~0.01ms

**Socket Protocol (JSON Lines for simplicity in v0.1):**
```json
{"type":"key","key":"a","modifiers":[]}
{"type":"swipe_start","x":100,"y":200}
{"type":"swipe_move","x":105,"y":198}
{"type":"swipe_end"}
{"type":"action","action":"paste"}
```

Future: migrate to protobuf or capnproto if perf matters.

### 4. Swipe Typing Engine

**Location:** `src/engine/swipe_engine.h`, `src/engine/swipe_engine.cpp`

**Full Specification:** [`docs/SWIPE_ENGINE_SPEC.md`](docs/SWIPE_ENGINE_SPEC.md)

This is the core language-agnostic swipe typing algorithm, designed to be:
- **Deterministic:** Same path → same candidates (unit-testable)
- **On-device:** No neural networks, no cloud calls
- **Geometry-first:** Spatial proximity drives key detection

**Key Components:**

1. **Path → Key Sequence:** Hysteresis-filtered mapping with bounce removal
2. **Shortlist Generation:** First/last char bucket index, length tolerance ±3
3. **Candidate Scoring:** Weighted combination of:
   - Edit distance (Levenshtein with early exit)
   - Bigram overlap
   - Word frequency (log-scaled)
   - Spatial distance from expected path
4. **Thresholds:** Minimum score filtering, max 8 candidates

**Usage from Engine:**

```cpp
#include "swipe_engine.h"

swipe::SwipeEngine engine;
engine.loadLayout("data/layouts/qwerty.json");
engine.loadDictionary("data/dict/words.txt", "data/dict/freq.tsv");

std::vector<swipe::Point> path = { /* from UI */ };
auto keySeq = engine.mapPathToSequence(path);
auto candidates = engine.generateCandidates(joinKeys(keySeq));
```

---

## Show/Hide Mechanism

### Trigger: Input Context Activation

**Source of Truth:** Fcitx5 input context lifecycle events.

| Event | Engine Action | UI Effect |
|-------|---------------|-----------|
| Context focus-in / enable | Send "show" via D-Bus | Window becomes visible |
| Context focus-out / disable | Send "hide" via D-Bus | Window hides |
| Context destroyed | Send "hide" + clear state | Window hides, composition reset |

**Why This Works on Wayland:**
- We respond to sanctioned IM protocol events
- No global key grabbing
- No compositor focus observation hacks

### Failure Handling

| Failure | Detection | Mitigation |
|---------|-----------|------------|
| UI process crashed | D-Bus name vanished / socket EOF | Engine restarts UI lazily on next activation |
| Rapid focus changes | Multiple activate/deactivate in <100ms | Debounce: delay hide by 50-100ms |
| App doesn't use IM module | No context activation | Nothing we can do; document requirement |

---

## Copy/Paste Implementation

> **Detailed Design:** See `.agent/CLIPBOARD_SHORTCUT_AGENT.md` for complete specification.

### Strategy: Shortcut Forwarding via IM

All clipboard operations use `InputContext::forwardKey()` to send the appropriate
keystroke through the sanctioned IM protocol. This is Wayland-safe and does not
require compositor privileges.

```cpp
void MagicKeyboardEngine::handleShortcutAction(const std::string& action) {
    auto* ic = currentIC_;
    if (!ic) return;
    
    // Auto-detect terminals (Konsole, Alacritty, etc.)
    bool useShift = isTerminal(ic->program()) && 
                    (action == "copy" || action == "paste");
    
    fcitx::KeySym sym;
    if (action == "copy") sym = FcitxKey_c;
    else if (action == "paste") sym = FcitxKey_v;
    else if (action == "cut") sym = FcitxKey_x;
    else if (action == "selectall") sym = FcitxKey_a;
    else return;
    
    fcitx::KeyStates states = fcitx::KeyState::Ctrl;
    if (useShift) states |= fcitx::KeyState::Shift;
    
    fcitx::Key key(sym, states);
    ic->forwardKey(key, /* isRelease */ false);
    ic->forwardKey(key, /* isRelease */ true);
}
```

### Terminal Auto-Detection

Terminals use Ctrl+Shift+C/V (because Ctrl+C sends SIGINT). The engine
automatically detects terminals by checking `ic->program()` against a known list:

- konsole, yakuake, gnome-terminal, alacritty, kitty, foot, wezterm, xterm, etc.

### Edge Cases

| Context | Copy/Paste | Notes |
|---------|------------|-------|
| Normal apps | Ctrl+C/V | Standard shortcuts |
| Terminals | Ctrl+Shift+C/V | Auto-detected |
| Password fields | Block copy/cut | Security; paste allowed |
| Read-only fields | Block paste/cut | Allow copy/selectall |

---

## Data Files

### Layout: `data/layouts/qwerty.json`

```json
{
  "name": "QWERTY",
  "rows": [
    {"keys": ["q","w","e","r","t","y","u","i","o","p"], "y": 0},
    {"keys": ["a","s","d","f","g","h","j","k","l"], "y": 1, "offset": 0.25},
    {"keys": ["shift","z","x","c","v","b","n","m","backspace"], "y": 2},
    {"keys": ["123","emoji","space",".",",enter"], "y": 3}
  ],
  "keyWidth": 48,
  "keyHeight": 56,
  "keySpacing": 4
}
```

### Dictionary: `data/dictionaries/en_US.txt` (v0.2)

Word frequency list, one word per line with frequency score.

---

## Packaging Strategy

### v0.1-v0.3: Host Install

**No Flatpak for the IM engine.** IM frameworks integrate at session level; sandboxing breaks integration.

Install targets:
- Engine addon: `~/.local/lib/fcitx5/` or `/usr/lib/fcitx5/`
- UI binary: `~/.local/bin/` or `/usr/bin/`
- Data files: `~/.local/share/magic-keyboard/` or `/usr/share/magic-keyboard/`

### SteamOS Deployment

- Arch PKGBUILD for easy pacman install
- Install to `/usr/local/` to survive SteamOS updates (or `/opt/`)
- Document `steamos-readonly disable` requirement

---

## MVP Roadmap

### v0.1: Foundation

**Goal:** Click-to-type keyboard that commits text via Fcitx5.

| Deliverable | Description |
|-------------|-------------|
| Fcitx5 engine scaffold | Loads, logs context activation |
| D-Bus control interface | Show/Hide signals |
| Unix socket stub | Protocol defined, basic echo |
| QML keyboard | QWERTY layout, click sends KeyPressed |
| Text commit | Engine receives key, commits to app |
| Show/hide wiring | activate→Show, deactivate→Hide |
| Basic keys | Letters, backspace, enter, shift (toggle) |

**Test Matrix:**
- [ ] Kate (Qt/KDE)
- [ ] gedit (GTK)
- [ ] Firefox
- [ ] Chrome/Chromium
- [ ] VS Code (Electron)
- [ ] Konsole (terminal, text visible even if shortcuts differ)

### v0.2: Swipe Typing

**Goal:** Click-drag produces word candidates.

| Deliverable | Description |
|-------------|-------------|
| Swipe state machine | Idle → TapPending → Swiping → Complete |
| Gesture sampling | Deadzone, smoothing, resampling |
| Path→key mapping | Nearest-key, collapse repeats |
| Dictionary loader | English word frequency list |
| Candidate scoring | Spatial error + edit distance + frequency |
| Candidate bar UI | Top 5 words, tap to select |
| Swipe trail visual | Real-time path drawing |

### v0.3: Polish & Deploy

**Goal:** Daily-usable on SteamOS.

| Deliverable | Description |
|-------------|-------------|
| Copy/Paste keys | Dedicated keys wired to actions |
| Terminal mode | Toggle for Ctrl+Shift shortcuts |
| Settings | Fcitx5 config integration or standalone |
| Window positioning | Dock bottom, avoid focused field |
| PKGBUILD | Arch/SteamOS packaging |
| Install docs | Update-safe deployment guide |

---

## File Structure

```
magic-keyboard/
├── README.md
├── ARCHITECTURE.md              # This document
├── CMakeLists.txt
├── .gitignore
│
├── src/
│   ├── engine/                  # Fcitx5 addon
│   │   ├── CMakeLists.txt
│   │   ├── magickeyboard.h
│   │   ├── magickeyboard.cpp
│   │   ├── magickeyboard.conf.in
│   │   └── magickeyboard-addon.conf.in
│   │
│   ├── ui/                      # Qt6/QML UI
│   │   ├── CMakeLists.txt
│   │   ├── main.cpp
│   │   ├── KeyboardWindow.qml
│   │   └── qml.qrc
│   │
│   └── ipc/                     # Shared protocol
│       ├── CMakeLists.txt
│       └── protocol.h
│
├── data/
│   └── layouts/
│       └── qwerty.json
│
└── packaging/
    └── arch/
        └── PKGBUILD.template
```

---

## Revision History

| Date | Change |
|------|--------|
| 2024-12-28 | Initial architecture, corrected from D-Bus-only to Unix socket |
