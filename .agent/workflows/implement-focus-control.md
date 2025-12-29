---
description: Implement the Focus & Visibility Control state machine from the design spec
---

# Implement Focus & Visibility Control

**Status:** ✅ COMPLETED (2025-12-29)

This workflow implements the robust focus detection system defined in `.agent/FOCUS_VISIBILITY_CONTROL.md`.

## Implementation Notes

- **Completed:** All steps executed successfully
- **ReadOnly flag:** Does not exist in Fcitx5 API; using `NoOnScreenKeyboard` and `Sensitive` instead
- **Build verified:** `cmake --build build` succeeds
- **Installed:** `/usr/local/lib/fcitx5/libmagickeyboard.so`

### Operational Hardening (Step B) - 2025-12-29

1. **CMake install paths fixed:**
   - Uses GNUInstallDirs with proper fallback
   - Configure-time guard prevents root-level install
   - Paths: `share/fcitx5/addon/`, `share/fcitx5/inputmethod/`

2. **Single-client enforcement:**
   - Engine closes existing clients when new UI connects
   - Prevents connection storms from multiple UI instances

3. **UI exponential backoff:**
   - Reconnect starts at 100ms, caps at 5s
   - Backoff resets on successful connection

4. **Logging reduced:**
   - "UI connected" and "UI disconnected" logged once
   - Socket errors only logged on first attempt

## Prerequisites

- Read and understand `.agent/FOCUS_VISIBILITY_CONTROL.md`
- Working build of Magic Keyboard engine and UI

## Steps

### 1. Add VisibilityState enum to magickeyboard.h

Add the state enum above the class definition:

```cpp
enum class VisibilityState {
    Hidden,
    PendingShow,
    Visible,
    PendingHide
};
```

### 2. Add new member variables to MagicKeyboardEngine

Replace `bool keyboardVisible_` with:

```cpp
VisibilityState visibilityState_ = VisibilityState::Hidden;
fcitx::InputContext* pendingIC_ = nullptr;
std::unique_ptr<fcitx::EventSource> debounceTimer_;

// Debounce configuration (milliseconds)
static constexpr int DEBOUNCE_SHOW_MS = 50;
static constexpr int DEBOUNCE_HIDE_MS = 100;
static constexpr int WATCHDOG_MS = 500;
```

### 3. Implement debounced handleFocusIn

Replace the current `handleFocusIn` with the state machine version:

```cpp
void MagicKeyboardEngine::handleFocusIn(fcitx::InputContext* ic) {
    if (shuttingDown_) return;
    
    std::string program = ic ? ic->program() : "?";
    std::string reason;
    int show = shouldShowKeyboard(ic, reason);
    
    MKLOG(Info) << "FocusIn: " << program << " show=" << show 
                << " (" << reason << ") state=" << (int)visibilityState_;
    
    if (!show) {
        // Invalid IC for keyboard - force hide if visible
        if (visibilityState_ == VisibilityState::Visible ||
            visibilityState_ == VisibilityState::PendingHide) {
            cancelDebounce();
            executeHide();
        }
        return;
    }
    
    switch (visibilityState_) {
        case VisibilityState::Hidden:
            currentIC_ = ic;
            pendingIC_ = ic;
            visibilityState_ = VisibilityState::PendingShow;
            scheduleDebounce(VisibilityState::Visible, DEBOUNCE_SHOW_MS);
            break;
            
        case VisibilityState::PendingShow:
            // Another FocusIn - update target IC, reset timer
            currentIC_ = ic;
            pendingIC_ = ic;
            scheduleDebounce(VisibilityState::Visible, DEBOUNCE_SHOW_MS);
            break;
            
        case VisibilityState::PendingHide:
            // New focus arrived before hide completed - cancel hide
            MKLOG(Info) << "FocusIn during PendingHide - canceling hide";
            cancelDebounce();
            currentIC_ = ic;
            visibilityState_ = VisibilityState::Visible;
            // Already visible, no need to send show again
            break;
            
        case VisibilityState::Visible:
            // Already visible, just update IC
            currentIC_ = ic;
            break;
    }
}
```

### 4. Implement debounced handleFocusOut

```cpp
void MagicKeyboardEngine::handleFocusOut(fcitx::InputContext* ic) {
    if (shuttingDown_) return;
    
    std::string program = ic ? ic->program() : "?";
    MKLOG(Info) << "FocusOut: " << program << " state=" << (int)visibilityState_;
    
    switch (visibilityState_) {
        case VisibilityState::Hidden:
            // Already hidden, nothing to do
            break;
            
        case VisibilityState::PendingShow:
            // FocusOut before show completed - cancel show
            if (pendingIC_ == ic) {
                MKLOG(Info) << "FocusOut during PendingShow - canceling show";
                cancelDebounce();
                visibilityState_ = VisibilityState::Hidden;
                pendingIC_ = nullptr;
            }
            break;
            
        case VisibilityState::Visible:
            visibilityState_ = VisibilityState::PendingHide;
            scheduleDebounce(VisibilityState::Hidden, DEBOUNCE_HIDE_MS);
            break;
            
        case VisibilityState::PendingHide:
            // Already pending hide, no change needed
            break;
    }
    
    if (currentIC_ == ic) {
        currentIC_ = nullptr;
    }
}
```

### 5. Add debounce helper methods

```cpp
void MagicKeyboardEngine::scheduleDebounce(VisibilityState target, int delayMs) {
    cancelDebounce();
    
    debounceTimer_ = instance_->eventLoop().addTimeEvent(
        CLOCK_MONOTONIC,
        fcitx::now(CLOCK_MONOTONIC) + delayMs * 1000,
        0,
        [this, target](fcitx::EventSourceTime*, uint64_t) {
            if (shuttingDown_) return false;
            executeTransition(target);
            return false; // One-shot
        }
    );
}

void MagicKeyboardEngine::cancelDebounce() {
    debounceTimer_.reset();
}

void MagicKeyboardEngine::executeTransition(VisibilityState target) {
    MKLOG(Debug) << "ExecuteTransition to " << (int)target;
    
    switch (target) {
        case VisibilityState::Visible:
            executeShow();
            break;
        case VisibilityState::Hidden:
            executeHide();
            break;
        default:
            break;
    }
}

void MagicKeyboardEngine::executeShow() {
    visibilityState_ = VisibilityState::Visible;
    pendingIC_ = nullptr;
    ensureUIRunning();
    sendToUI("{\"type\":\"show\"}\n");
    MKLOG(Debug) << "Keyboard SHOWN";
}

void MagicKeyboardEngine::executeHide() {
    visibilityState_ = VisibilityState::Hidden;
    pendingIC_ = nullptr;
    sendToUI("{\"type\":\"hide\"}\n");
    MKLOG(Debug) << "Keyboard HIDDEN";
}
```

### 6. Update watchdog to use state machine

```cpp
void MagicKeyboardEngine::startWatchdog() {
    watchdogTimer_ = instance_->eventLoop().addTimeEvent(
        CLOCK_MONOTONIC,
        fcitx::now(CLOCK_MONOTONIC) + WATCHDOG_MS * 1000,
        0,
        [this](fcitx::EventSourceTime* source, uint64_t) {
            if (shuttingDown_) return false;
            
            if (visibilityState_ == VisibilityState::Visible ||
                visibilityState_ == VisibilityState::PendingHide) {
                
                auto* ic = instance_->inputContextManager().lastFocusedInputContext();
                if (!ic || !ic->hasFocus()) {
                    MKLOG(Info) << "Watchdog: no focused IC, forcing hide";
                    cancelDebounce();
                    executeHide();
                }
            }
            
            source->setTime(fcitx::now(CLOCK_MONOTONIC) + WATCHDOG_MS * 1000);
            return true;
        }
    );
}
```

### 7. Update header file declarations

Add to private section of MagicKeyboardEngine:

```cpp
void scheduleDebounce(VisibilityState target, int delayMs);
void cancelDebounce();
void executeTransition(VisibilityState target);
void executeShow();
void executeHide();
```

### 8. Enhance shouldShowKeyboard with additional capability checks

Add ReadOnly check to shouldShowKeyboard:

```cpp
// Add after Password check:
if (caps.test(fcitx::CapabilityFlag::ReadOnly)) {
    reason = "readonly";
    return 0;
}
```

### 9. Update destructor to cancel debounce timer

In `~MagicKeyboardEngine()`:

```cpp
debounceTimer_.reset();
```

### 10. Build and test

// turbo
```bash
cd /home/deck/Pictures/magic\ kb/magic-keyboard
cmake --build build
```

### 11. Test focus transitions

- Open Kate, click text area → keyboard should show
- Click Kate sidebar → keyboard should hide
- Rapidly click between text and sidebar → no flicker
- Tab between fields → keyboard stays visible
- Focus password field → keyboard hides

## Verification

After implementation, run these verification tests:

### Step A: Verify Install Paths

```bash
# Should show correct paths, NOT /addon or /inputmethod
ls -la /usr/local/share/fcitx5/addon/magickeyboard.conf
ls -la /usr/local/share/fcitx5/inputmethod/magickeyboard.conf
ls -la /usr/local/lib/fcitx5/libmagickeyboard.so

# Should fail (no root-level directories)
ls /addon 2>&1 | grep "No such file"
ls /inputmethod 2>&1 | grep "No such file"
```

### Step B: Verify Single UI Instance

```bash
# Restart fcitx5 and wait
pkill fcitx5 && sleep 1 && fcitx5 -d

# Wait 10 seconds and check for connection storms
sleep 10 && journalctl --user --since "15 seconds ago" | grep -c "UI connected"
# Result should be 0 or 1, not many

# Check no multiple UI processes
ps aux | grep -c magickeyboard-ui
# Result should be 0 (no UI until focus) or 1 (when focus active)
```

### Step C: Focus/Visibility Acceptance Tests

1. Open Kate, click text area → keyboard shows (after ~50ms)
2. Click Kate sidebar → keyboard hides (after ~100ms)
3. Rapid click between text and sidebar → **no flicker**
4. Tab between fields → keyboard stays visible
5. Focus password field → keyboard hides
6. Focus sensitive/private field → keyboard hides

### Step D: Journalctl Monitoring

```bash
# Watch logs while testing
journalctl --user -f | grep -i magic
```

Expected logs:
- `Magic Keyboard engine starting`
- `Socket: /run/user/1000/magic-keyboard.sock`
- `Magic Keyboard engine ready`
- `Registered input method: magic-keyboard`
- `UI connected` (once per session)
- `FocusIn: ...` with state transitions
- No "UI connected" spam

