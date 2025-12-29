# Focus & Visibility Control Agent

**Magic Keyboard - Critical Subsystem Design Document**

## Executive Summary

This document defines a **robust focus detection system** using Fcitx5 InputContext signals that safely controls keyboard visibility without ever stealing focus on Wayland. Focus handling is where most on-screen keyboards break on Wayland due to its security model restrictions.

---

## 1. Role & Mission Statement

The Focus & Visibility Control Agent guarantees:

1. **Show on Focus**: Keyboard becomes visible when a real text field gains focus
2. **Hide on Blur**: Keyboard disappears when focus leaves text input
3. **Never Steal Focus**: The keyboard window NEVER takes activation/focus from the target app
4. **No Flicker Loops**: Prevents rapid show/hide oscillation from edge cases

---

## 2. Wayland Security Model Constraints

### What We Cannot Do
- ❌ Observe compositor-private focus events directly
- ❌ Query which window has focus globally
- ❌ Inject keyboard/pointer input globally
- ❌ Use X11-style focus polling or XGetInputFocus

### What We Can Do
- ✅ Receive InputContext lifecycle events from Fcitx5 (sanctioned IM protocol)
- ✅ Create non-focusable tool windows with `Qt::WindowDoesNotAcceptFocus`
- ✅ Track per-application InputContext state via Fcitx5 API

---

## 3. Architecture Overview

```
┌────────────────────────────────────────────────────────────────────────┐
│                         KDE Plasma Compositor                           │
│                      (focus owner, pointer/kb routing)                  │
└─────────────────────────────────┬──────────────────────────────────────┘
                                  │
                                  │ Wayland text-input-v3 protocol
                                  ▼
┌────────────────────────────────────────────────────────────────────────┐
│                           Fcitx5 Daemon                                 │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │         InputContextManager (source of truth)                     │  │
│  │                                                                   │  │
│  │   InputContext per widget/field:                                  │  │
│  │     • program()     → owning application name                     │  │
│  │     • capabilityFlags() → Password, ReadOnly, etc.               │  │
│  │     • hasFocus()    → whether this IC has compositor focus        │  │
│  │     • frontend()    → wayland/xim/dbus etc.                       │  │
│  └───────────────────────────┬──────────────────────────────────────┘  │
│                              │                                          │
│              FocusIn / FocusOut events                                  │
│                              ▼                                          │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │              FOCUS VISIBILITY STATE MACHINE                       │  │
│  │                  (This Agent's Domain)                            │  │
│  │                                                                   │  │
│  │   States: HIDDEN → PENDING_SHOW → VISIBLE → PENDING_HIDE         │  │
│  │                                                                   │  │
│  │   Debounce timers prevent flicker                                 │  │
│  │   Watchdog ensures eventual hide on lost focus                    │  │
│  └───────────────────────────┬──────────────────────────────────────┘  │
│                              │                                          │
│                        show/hide messages                               │
│                              ▼                                          │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │                  Unix Domain Socket                               │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────┬──────────────────────────────────────┘
                                  │
                                  ▼
┌────────────────────────────────────────────────────────────────────────┐
│                     magickeyboard-ui (Qt6/QML)                          │
│                                                                         │
│   Window Flags:                                                         │
│     Qt::Tool | Qt::FramelessWindowHint |                                │
│     Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus             │
│                                                                         │
│   NEVER calls:                                                          │
│     • window->raise() with activation                                   │
│     • window->requestActivate()                                         │
│     • setFocus() on any child widget                                    │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Visibility State Machine

### 4.1 States

| State | Description | Window Visibility |
|-------|-------------|-------------------|
| `HIDDEN` | No active text field, keyboard hidden | hide() |
| `PENDING_SHOW` | FocusIn received, waiting debounce | hide() |
| `VISIBLE` | Active text field, keyboard shown | show() |
| `PENDING_HIDE` | FocusOut received, waiting debounce | show() |

### 4.2 State Diagram

```
                    ┌─────────────────────────────────────┐
                    │                                     │
                    ▼                                     │
              ┌──────────┐   FocusIn(valid)         ┌─────┴──────┐
              │  HIDDEN  │──────────────────────────►│PENDING_SHOW│
              └────▲─────┘                          └──────┬─────┘
                   │                                       │
                   │                           debounce_ms │
                   │                           (50ms)      │
                   │                                       ▼
              ┌────┴──────┐   FocusOut          ┌──────────────┐
              │PENDING_HIDE│◄───────────────────│   VISIBLE    │
              └─────┬─────┘                     └──────▲───────┘
                    │                                  │
        debounce_ms │                                  │
        (100ms)     │                                  │
                    ▼                                  │
              ┌──────────┐   FocusIn(valid)           │
              │  HIDDEN  │────────────────────────────┘
              └──────────┘

   Additional transitions:
   - PENDING_SHOW + FocusOut(same IC) → HIDDEN (cancel pending show)
   - PENDING_HIDE + FocusIn(any valid) → VISIBLE (cancel pending hide)
   - Any state + shouldShowKeyboard()=false → HIDDEN (immediate)
   - Watchdog timeout in VISIBLE with no focus → HIDDEN (safety)
```

### 4.3 Implementation

```cpp
enum class VisibilityState {
    Hidden,
    PendingShow,
    Visible,
    PendingHide
};

class FocusVisibilityController {
public:
    // Configuration (tune for responsiveness vs flicker)
    static constexpr int DEBOUNCE_SHOW_MS = 50;   // Wait before showing
    static constexpr int DEBOUNCE_HIDE_MS = 100;  // Wait before hiding
    static constexpr int WATCHDOG_MS = 500;       // Safety check interval

private:
    VisibilityState state_ = VisibilityState::Hidden;
    fcitx::InputContext* pendingIC_ = nullptr;
    std::unique_ptr<fcitx::EventSource> debounceTimer_;
    std::unique_ptr<fcitx::EventSource> watchdogTimer_;
    
    void transitionTo(VisibilityState newState, fcitx::InputContext* ic = nullptr);
};
```

---

## 5. Trigger Conditions (Exact Specification)

### 5.1 Focus-In Trigger

A FocusIn event triggers `PENDING_SHOW` → `VISIBLE` if ALL of the following are true:

```cpp
int shouldShowKeyboard(fcitx::InputContext* ic, std::string& reason) {
    // 1. Valid InputContext
    if (!ic) {
        reason = "null-ic";
        return 0;
    }
    
    // 2. InputContext belongs to Magic Keyboard IM
    //    (Per-IC entry is authoritative; global fallback only if null)
    const auto* entry = instance_->inputMethodEntry(ic);
    bool isOurs = (entry && entry->addon() == "magickeyboard");
    if (!entry) {
        std::string globalIM = instance_->currentInputMethod();
        isOurs = (globalIM == "magic-keyboard");
    }
    if (!isOurs) {
        reason = "other-im";
        return 0;
    }
    
    // 3. Not a password field
    auto caps = ic->capabilityFlags();
    if (caps.test(fcitx::CapabilityFlag::Password)) {
        reason = "password";
        return 0;
    }
    
    // 4. Respect NoOnScreenKeyboard hint (Qt apps can request this)
    if (caps.test(fcitx::CapabilityFlag::NoOnScreenKeyboard)) {
        reason = "no-osk-hint";
        return 0;
    }
    
    // 5. Not a sensitive field (privacy mode)
    if (caps.test(fcitx::CapabilityFlag::Sensitive)) {
        reason = "sensitive";
        return 0;
    }
    
    reason = "ok";
    return 1;
}
```

### 5.2 Focus-Out Trigger

A FocusOut event triggers `PENDING_HIDE` if:
- The keyboard is currently `VISIBLE`
- The lost InputContext matches or predecessors the current active IC

### 5.3 Edge Cases

| Scenario | Behavior |
|----------|----------|
| FocusIn → FocusOut within 50ms | Cancel show, stay HIDDEN |
| FocusOut → FocusIn within 100ms (same app, different field) | Cancel hide, stay VISIBLE |
| FocusOut → FocusIn within 100ms (different app) | Cancel hide, re-validate, VISIBLE if valid |
| Multiple rapid FocusIn events | Each resets debounce timer, only final wins |
| App crash / compositor restart | Watchdog detects no hasFocus(), force HIDDEN |

---

## 6. Differentiating Real Text Fields vs Fake Focus

### 6.1 Real Text Field Indicators

| Signal | Meaning | Action |
|--------|---------|--------|
| `CapabilityFlag::Preedit` | Supports composition | Show keyboard |
| `CapabilityFlag::FormattedPreedit` | Rich preedit | Show keyboard |
| `CapabilityFlag::SurroundingText` | Provides context | Show keyboard |
| None of the above but not Password | Generic text input | Show keyboard |

### 6.2 Fake/Special Field Detection

| Signal | Meaning | Action |
|--------|---------|--------|
| `CapabilityFlag::Password` | Password mode active | **HIDE keyboard** |
| `CapabilityFlag::ReadOnly` | Non-editable field | Consider hiding |
| `CapabilityFlag::Sensitive` | Privacy mode | Hide keyboard |
| `program()` = "" or unknown | Transient/fake IC | Extra caution |

### 6.3 Application-Specific Quirks

```cpp
// Known quirk database (expand as needed)
struct AppQuirk {
    std::string program;
    enum Action { Normal, AlwaysShow, AlwaysHide, DelayedShow };
    Action action;
    int extraDelayMs;
};

std::vector<AppQuirk> knownQuirks = {
    // Electron apps may send spurious focus events
    {"electron", AppQuirk::DelayedShow, 100},
    {"code", AppQuirk::DelayedShow, 100},
    
    // Terminal emulators: show anyway (user explicitly chose OSK)
    {"konsole", AppQuirk::Normal, 0},
    {"alacritty", AppQuirk::Normal, 0},
    
    // Games: hide unless explicitly text field
    {"steam", AppQuirk::Normal, 0},
};

AppQuirk getQuirk(const std::string& program) {
    for (const auto& q : knownQuirks) {
        if (program.find(q.program) != std::string::npos) {
            return q;
        }
    }
    return {"", AppQuirk::Normal, 0};
}
```

---

## 7. Browser, Electron, and Terminal Edge Cases

### 7.1 Browser Handling (Firefox, Chrome)

**Problem**: Browsers may create single IC for entire window, then signal focus changes internally.

**Solution**:
- Trust FocusIn/FocusOut events from browser IC
- Note: `program()` returns "firefox" or "chromium"
- Password detection via `CapabilityFlag::Password` works correctly
- Consider: URL bar gets focus but is text input → show keyboard

**Verified Behavior** (from README test matrix):
- ✅ Firefox URL bar → keyboard shows
- ✅ Firefox password field → keyboard hides
- ✅ Firefox content text area → keyboard shows

### 7.2 Electron Apps (VS Code, Discord, Slack)

**Problem**: Electron uses Chromium's IME integration which can be inconsistent.

**Mitigation**:
```cpp
void handleFocusIn(fcitx::InputContext* ic) {
    auto quirk = getQuirk(ic->program());
    
    if (quirk.action == AppQuirk::DelayedShow) {
        // Extra debounce for Electron apps
        stateDebounceMs_ = DEBOUNCE_SHOW_MS + quirk.extraDelayMs;
    }
    
    // ... continue with normal flow
}
```

### 7.3 Terminal Emulators (Konsole, Alacritty)

**Problem**: Terminals are always text input but use Ctrl+Shift+C/V for copy/paste.

**Solution**:
- Show keyboard by default (terminals send proper IC events)
- Terminal Mode toggle for copy/paste shortcuts (v0.3)
- Detection: `program()` contains "konsole", "terminal", "alacritty", etc.

```cpp
bool isTerminal(const std::string& program) {
    static const std::vector<std::string> terminals = {
        "konsole", "alacritty", "kitty", "gnome-terminal",
        "xterm", "terminator", "tilix", "terminology"
    };
    for (const auto& t : terminals) {
        if (program.find(t) != std::string::npos) return true;
    }
    return false;
}
```

---

## 8. Preventing Focus Stealing

### 8.1 Window Flag Enforcement (UI Side)

```cpp
// In magickeyboard-ui main.cpp
window->setFlags(
    Qt::Tool |                        // Utility window, not main app
    Qt::FramelessWindowHint |         // No window decorations
    Qt::WindowStaysOnTopHint |        // Always on top
    Qt::WindowDoesNotAcceptFocus      // CRITICAL: Never take focus
);

// NEVER call these:
// window->raise();           // May steal focus on some compositors
// window->requestActivate(); // Explicitly requests focus
// widget->setFocus();        // Focus within window
```

### 8.2 Wayland-Specific: No Activation Requests

```cpp
// On KDE Plasma/KWin Wayland:
// - Qt::WindowDoesNotAcceptFocus is respected
// - No need for layer-shell (that's wlroots-specific)
// - Standard tool window semantics apply

// Test: Click keyboard key → focus remains on text field
void KeyboardWindowController::onKeyClicked(const QString& key) {
    // Just send message to engine; do NOT touch focus
    socket_->write(...);
}
```

### 8.3 X11 Fallback (for hybrid mode)

```cpp
#ifdef Q_WS_X11
// Also set X11-specific hints for compositors that check them
Atom wmHints = XInternAtom(display, "_MOTIF_WM_HINTS", False);
// Set InputHint = False
// Set _NET_WM_WINDOW_TYPE_UTILITY
#endif
```

---

## 9. Flicker Prevention

### 9.1 Debounce Implementation

```cpp
void FocusVisibilityController::scheduleTransition(
    VisibilityState target, 
    int delayMs
) {
    // Cancel any pending transition
    debounceTimer_.reset();
    
    debounceTimer_ = instance_->eventLoop().addTimeEvent(
        CLOCK_MONOTONIC,
        fcitx::now(CLOCK_MONOTONIC) + delayMs * 1000,
        0,
        [this, target](fcitx::EventSourceTime*, uint64_t) {
            executeTransition(target);
            return false; // One-shot
        }
    );
}

void handleFocusIn(fcitx::InputContext* ic) {
    if (state_ == VisibilityState::PendingHide) {
        // Cancel pending hide, immediately show
        debounceTimer_.reset();
        executeTransition(VisibilityState::Visible);
        return;
    }
    
    if (state_ == VisibilityState::Hidden) {
        pendingIC_ = ic;
        state_ = VisibilityState::PendingShow;
        scheduleTransition(VisibilityState::Visible, DEBOUNCE_SHOW_MS);
    }
}

void handleFocusOut(fcitx::InputContext* ic) {
    if (state_ == VisibilityState::PendingShow && pendingIC_ == ic) {
        // Cancel pending show before it fires
        debounceTimer_.reset();
        state_ = VisibilityState::Hidden;
        return;
    }
    
    if (state_ == VisibilityState::Visible) {
        state_ = VisibilityState::PendingHide;
        scheduleTransition(VisibilityState::Hidden, DEBOUNCE_HIDE_MS);
    }
}
```

### 9.2 Flicker Scenarios Prevented

| Scenario | Without Debounce | With Debounce |
|----------|------------------|---------------|
| Tab between fields (50ms) | SHOW → HIDE → SHOW | SHOW (uninterrupted) |
| Click sidebar then back to text (100ms) | SHOW → HIDE → SHOW | SHOW (uninterrupted) |
| Electron field init (100ms noise) | Multiple show/hide | Single SHOW after settle |
| Fast app switch | HIDE → SHOW → HIDE | Single final state |

---

## 10. Watchdog Safety Net

The watchdog provides a fallback in case focus tracking fails:

```cpp
void startWatchdog() {
    watchdogTimer_ = instance_->eventLoop().addTimeEvent(
        CLOCK_MONOTONIC,
        fcitx::now(CLOCK_MONOTONIC) + WATCHDOG_MS * 1000,
        0,
        [this](fcitx::EventSourceTime* source, uint64_t) {
            if (shuttingDown_) return false;
            
            if (state_ == VisibilityState::Visible || 
                state_ == VisibilityState::PendingHide) {
                
                // Verify we still have a valid focused IC
                auto* ic = instance_->inputContextManager()
                              .lastFocusedInputContext();
                
                if (!ic || !ic->hasFocus()) {
                    MKLOG(Info) << "Watchdog: focus lost, forcing hide";
                    debounceTimer_.reset();
                    executeTransition(VisibilityState::Hidden);
                }
            }
            
            // Reschedule
            source->setTime(fcitx::now(CLOCK_MONOTONIC) + WATCHDOG_MS * 1000);
            return true;
        }
    );
}
```

### Watchdog Triggers

| Condition | Watchdog Action |
|-----------|-----------------|
| `lastFocusedInputContext()` returns null | Force HIDDEN |
| IC exists but `hasFocus()` = false | Force HIDDEN |
| IC exists but `shouldShowKeyboard()` = false | Force HIDDEN |
| All normal | No action, reschedule |

---

## 11. Failure Mitigation Logic

### 11.1 Failure Modes & Recovery

| Failure | Detection | Mitigation |
|---------|-----------|------------|
| UI process crashed | Socket EOF received | Engine clears client, lazy restart on next FocusIn |
| Engine crashed | Socket connection fails | UI reconnects every 1000ms |
| Rapid focus changes | Multiple events < 100ms | Debounce timers collapse to single action |
| App doesn't use IM module | No FocusIn ever fired | Nothing we can do; documented limitation |
| Compositor restart | All ICs destroyed | Watchdog hides, new ICs created on reactivation |
| Fcitx5 restart | Engine reloads | UI reconnects, state resets to HIDDEN |

### 11.2 Graceful Degradation

```cpp
// If socket send fails repeatedly, don't block engine
void sendToUI(const std::string& msg) {
    static int consecutiveFailures = 0;
    
    for (auto& [fd, client] : clients_) {
        ssize_t n = write(fd, msg.c_str(), msg.size());
        if (n < 0) {
            consecutiveFailures++;
            if (consecutiveFailures > 10) {
                MKLOG(Warn) << "Too many send failures, closing client";
                removeClient(fd);
            }
        } else {
            consecutiveFailures = 0;
        }
    }
}
```

---

## 12. Testing Matrix

### 12.1 Focus Event Tests

| Test | Steps | Expected |
|------|-------|----------|
| Basic show | Click Kate text area | Keyboard shows within 100ms |
| Basic hide | Click Kate sidebar | Keyboard hides within 150ms |
| Tab between fields | Alt-Tab, then Tab key | Keyboard stays visible |
| Password field | Focus password input | Keyboard hides |
| Leave password | Focus normal field after password | Keyboard shows |
| App switch | Alt-Tab to non-text app | Keyboard hides |
| Return to text app | Alt-Tab back to Kate text area | Keyboard shows |

### 12.2 Flicker Tests

| Test | Steps | Expected |
|------|-------|----------|
| Quick click away and back | Click sidebar, immediately click text (<100ms) | No flicker, keyboard stays visible |
| Electron focus noise | Open VS Code, click into editor | Single show, no flicker |
| Browser navigation | Click Firefox URL bar, then page content | Correct show/hide, no repeated flicker |

### 12.3 Focus Stealing Tests

| Test | Steps | Expected |
|------|-------|----------|
| Click keyboard key | While typing, click 'a' key | Character commits, Kate keeps focus |
| Drag swipe | Swipe across keyboard | Trail shows, Kate keeps focus |
| Candidate tap | Tap word in candidate bar | Word commits, Kate keeps focus |
| Manual toggle | Run `magickeyboardctl show` | Keyboard shows, no app loses focus |

### 12.4 Edge Case Tests

| Test | Steps | Expected |
|------|-------|----------|
| Konsole focus | Open Konsole, focus terminal | Keyboard shows (terminal is text input) |
| Plasma panel click | Click taskbar | Keyboard hides |
| Lock screen | Lock → unlock | Keyboard hidden; shows only when text field focused |
| Fcitx5 restart | `pkill fcitx5; fcitx5 -d` | UI reconnects, keyboard hidden until focus |

---

## 13. Key Outputs Summary

### 13.1 Exact Trigger Conditions
- **Show**: FocusIn + valid IC + our IM addon + not password + not readonly
- **Hide**: FocusOut + visible + debounce expired
- **Emergency Hide**: Watchdog finds no valid focused IC

### 13.2 State Machine
```
HIDDEN ←→ PENDING_SHOW ←→ VISIBLE ←→ PENDING_HIDE
     ↑                                          │
     └──────────────────────────────────────────┘
```

Debounce timers:
- SHOW delay: 50ms (fast enough to feel responsive)
- HIDE delay: 100ms (prevent flicker on field switch)
- Watchdog: 500ms (safety net, not primary)

### 13.3 Failure Mitigation
- Socket reconnect: 1000ms retry loop
- Watchdog: 500ms verification cycle  
- Debounce: Collapses rapid events
- Graceful degradation: Engine functions without UI

---

## 14. Implementation Checklist

- [ ] Implement `VisibilityState` enum and state variable
- [ ] Add `debounceTimer_` member to engine
- [ ] Refactor `handleFocusIn` to use debounced show
- [ ] Refactor `handleFocusOut` to use debounced hide
- [ ] Add debounce cancellation on opposite event
- [ ] Verify `shouldShowKeyboard()` checks all capability flags
- [ ] Test watchdog with artificially broken IC
- [ ] Test on Wayland with KDE Plasma
- [ ] Test with Firefox, VS Code, Konsole
- [ ] Measure show/hide latency (target: <100ms perceived)

---

## Revision History

| Date | Change |
|------|--------|
| 2024-12-29 | Initial Focus & Visibility Control design document |
