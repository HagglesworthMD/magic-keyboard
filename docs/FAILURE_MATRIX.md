# Magic Keyboard Failure Matrix & Recovery Strategies

**QA / Failure-Mode Analysis**
**Version:** 1.0
**Date:** 2025-12-29

---

## Executive Summary

This document analyzes failure modes for the Magic Keyboard Fcitx5 addon on SteamOS. It categorizes failures by subsystem, severity, and recovery path. The goal is to ensure the keyboard remains usable even when components fail, and provides clear debugging paths.

---

## Failure Classification

| Severity | Definition | Impact |
|----------|------------|--------|
| **P0** | System-breaking | Keyboard completely non-functional, user cannot type |
| **P1** | Major feature loss | Core functionality impaired (e.g., no swipe, no candidates) |
| **P2** | Minor degradation | Inconvenience but workarounds exist |
| **P3** | Cosmetic | Visual glitches, logging noise |

---

## 1. Focus Edge Cases

### 1.1 Focus Flicker Loop

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Rapid FocusIn/FocusOut events cause keyboard show/hide oscillation |
| **Root Cause** | Application or compositor sending rapid focus events; keyboard window briefly stealing focus before `WindowDoesNotAcceptFocus` takes effect |
| **Severity** | P1 |
| **Detection** | `journalctl --user -u fcitx5` shows repeated FocusIn/FocusOut within <100ms |
| **Current Mitigation** | Watchdog timer in `startWatchdog()` (500ms check) |
| **Recovery Strategy** | Debounce hide by 50-100ms; only hide if still unfocused after delay |
| **Status** | ⚠️ Partial - watchdog catches stale state but no hide debounce |

```cpp
// RECOMMENDED: Add to handleFocusOut()
// Schedule hide after 80ms, cancel if FocusIn arrives
hideDebounceTimer_ = instance_->eventLoop().addTimeEvent(
    CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 80000, 0,
    [this](fcitx::EventSourceTime*, uint64_t) {
        if (!currentIC_ || !currentIC_->hasFocus()) {
            hideKeyboard();
        }
        return false;
    });
```

### 1.2 Password Field Misdetection

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Keyboard shows on password fields (security risk) |
| **Root Cause** | Application doesn't set `CapabilityFlag::Password` |
| **Severity** | P2 (security-sensitive P1 if credentials exposed) |
| **Detection** | Log `FocusIn: <program> show=1 (ok)` when in password field |
| **Current Mitigation** | Check `caps.test(fcitx::CapabilityFlag::Password)` in `shouldShowKeyboard()` |
| **Recovery Strategy** | None available - depends on application correctly setting flags |
| **Status** | ✅ Implemented (but app-dependent) |

### 1.3 No Active InputContext on Commit

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Key pressed but `currentIC_` is null, text lost |
| **Root Cause** | Focus event arrived but IC pointer was cleared; race between UI click and focus change |
| **Severity** | P1 |
| **Detection** | Log `Key but no active IC` |
| **Current Mitigation** | Log warning in `handleKeyPress()` |
| **Recovery Strategy** | Fall back to `lastFocusedInputContext()` (line 559 uses this for candidates) |
| **Status** | ⚠️ Inconsistent - `handleKeyPress` uses `currentIC_`, candidate commit uses `lastFocusedInputContext()` |

```cpp
// RECOMMENDED: Unify IC resolution
fcitx::InputContext* getActiveIC() {
    if (currentIC_ && currentIC_->hasFocus()) return currentIC_;
    auto *last = instance_->inputContextManager().lastFocusedInputContext();
    if (last && last->hasFocus()) return last;
    return nullptr;
}
```

### 1.4 App Doesn't Use IM Module

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Text field focused but no Fcitx5 activation event |
| **Root Cause** | Application not compiled with GTK_IM_MODULE/QT_IM_MODULE support, or uses custom input handling |
| **Severity** | P0 (per-app) |
| **Detection** | No `FocusIn` log when clicking text field in specific app |
| **Current Mitigation** | None |
| **Recovery Strategy** | Document requirement; provide manual show/hide via `magickeyboardctl` |
| **Status** | ✅ Manual override available via desktop shortcuts |

---

## 2. Race Conditions

### 2.1 Engine Shutdown During Active Socket Write

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Segfault or undefined behavior during shutdown |
| **Root Cause** | Event callback runs after member variables destroyed |
| **Severity** | P0 |
| **Detection** | Crash on fcitx5 shutdown |
| **Current Mitigation** | `shuttingDown_` atomic flag checked in all callbacks; destructor order enforced by declaration order |
| **Recovery Strategy** | N/A - crash is the failure |
| **Status** | ✅ Implemented (lines 50, 64, 176, 196, 642, 693, 706) |

### 2.2 Multiple Clients Racing on Socket

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Messages interleaved; partial JSON lines |
| **Root Cause** | Multiple clients connect (UI + magickeyboardctl) and send concurrently |
| **Severity** | P2 |
| **Detection** | JSON parse errors in log |
| **Current Mitigation** | Per-client buffer in `Client` struct (line 70-73) |
| **Recovery Strategy** | Buffers are isolated per-client, newline-delimited parsing handles partial reads |
| **Status** | ✅ Implemented |

### 2.3 UI Connect During Engine Startup

| Aspect | Details |
|--------|---------|
| **Failure Mode** | UI connects before socket server ready |
| **Root Cause** | `startSocketServer()` hasn't completed when UI spawned |
| **Severity** | P2 |
| **Detection** | UI log `Socket error: Connection refused` |
| **Current Mitigation** | UI has 1-second reconnect timer (`scheduleReconnect()`) |
| **Recovery Strategy** | UI automatically retries; no data loss since keyboard starts hidden |
| **Status** | ✅ Implemented |

### 2.4 Candidate State vs Key Event Race

| Aspect | Details |
|--------|---------|
| **Failure Mode** | User taps key while in candidate mode; wrong text committed |
| **Root Cause** | `candidateMode_` checked before candidate committed |
| **Severity** | P2 |
| **Detection** | Unexpected text output sequence |
| **Current Mitigation** | Implicit commit in `handleKeyPress()` (lines 294-302) |
| **Recovery Strategy** | Always commit top candidate before processing new key |
| **Status** | ✅ Implemented |

---

## 3. Startup/Shutdown Ordering

### 3.1 Engine Starts Before Wayland Session Ready

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Socket path unavailable; UI launch fails |
| **Root Cause** | `XDG_RUNTIME_DIR` not set; Wayland compositor not running |
| **Severity** | P0 |
| **Detection** | `socket() failed` or path is `/tmp/...` in logs |
| **Current Mitigation** | Fallback to `/tmp/` in `getSocketPath()` |
| **Recovery Strategy** | Systemd dependency on `graphical-session.target` |
| **Status** | ⚠️ Partial - fallback works but less secure |

### 3.2 UI Starts But Engine Not Running

| Aspect | Details |
|--------|---------|
| **Failure Mode** | UI window exists but cannot communicate |
| **Root Cause** | Fcitx5 not started or magickeyboard addon not loaded |
| **Severity** | P1 |
| **Detection** | UI log loops with `Connecting to: ...` then `Socket error` |
| **Current Mitigation** | Infinite reconnect loop (1s interval) |
| **Recovery Strategy** | Consider max retry count + notification to user |
| **Status** | ⚠️ Could spam logs indefinitely |

**RECOMMENDATION:**
```cpp
void scheduleReconnect() {
    reconnectAttempts_++;
    if (reconnectAttempts_ > 30) {  // 30 seconds
        qCritical() << "Engine unreachable after 30 attempts, giving up";
        return;  // Or show error overlay
    }
    reconnectTimer_->setInterval(std::min(1000 * reconnectAttempts_, 5000));
    reconnectTimer_->start();
}
```

### 3.3 Engine Destructor Ordering

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Use-after-free in event callbacks |
| **Root Cause** | Member variables destroyed in wrong order |
| **Severity** | P0 |
| **Detection** | ASAN/Valgrind reports or random crashes |
| **Current Mitigation** | Explicit `.reset()` order in destructor; declaration order enforces construction/destruction sequence |
| **Recovery Strategy** | Comments in header (lines 52-54) document required ordering |
| **Status** | ✅ Implemented |

### 3.4 UI Process Orphaned After Engine Crash

| Aspect | Details |
|--------|---------|
| **Failure Mode** | UI remains visible/running but engine dead |
| **Root Cause** | Engine crashes; UI has no heartbeat |
| **Severity** | P2 |
| **Detection** | UI shows but typing does nothing |
| **Current Mitigation** | Socket disconnect triggers auto-hide (future: should hide) |
| **Recovery Strategy** | On disconnect, hide UI and show "Reconnecting..." indicator |
| **Status** | ⚠️ UI stays up, keeps trying to reconnect silently |

---

## 4. UI Crash Recovery

### 4.1 QML Load Failure

| Aspect | Details |
|--------|---------|
| **Failure Mode** | UI process exits immediately |
| **Root Cause** | Missing QML file, Qt version mismatch, GPU driver issue |
| **Severity** | P0 |
| **Detection** | `Failed to load QML` in logs |
| **Current Mitigation** | `QCoreApplication::exit(-1)` (line 235) |
| **Recovery Strategy** | Engine's `ensureUIRunning()` will respawn on next focus |
| **Status** | ✅ Lazy respawn implemented |

### 4.2 UI Segfault/SIGKILL

| Aspect | Details |
|--------|---------|
| **Failure Mode** | UI disappears without proper shutdown |
| **Root Cause** | Bug, OOM killer, corrupted memory |
| **Severity** | P1 |
| **Detection** | Socket EOF in engine; `waitpid()` returns non-zero |
| **Current Mitigation** | `ensureUIRunning()` checks `uiPid_` status (lines 246-251) |
| **Recovery Strategy** | Auto-respawn on next focus event |
| **Status** | ✅ Implemented |

### 4.3 GPU Context Loss

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Keyboard renders as black rectangle |
| **Root Cause** | Compositor restart, GPU reset, suspend/resume |
| **Severity** | P2 |
| **Detection** | Visual inspection (hard to detect programmatically) |
| **Current Mitigation** | None |
| **Recovery Strategy** | Add `AA_ShareOpenGLContexts` (already set line 215); consider forcing window recreate on visibility |
| **Status** | ⚠️ Untested |

### 4.4 Window Focus Steal Despite Flags

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Keyboard window takes focus, text field loses focus |
| **Root Cause** | Qt/compositor bug; flags not honored |
| **Severity** | P0 |
| **Detection** | Log shows `FocusOut` immediately after keyboard appears |
| **Current Mitigation** | `Qt::WindowDoesNotAcceptFocus` flag (line 244) |
| **Recovery Strategy** | Add X11-specific hints (`WM_HINTS InputHint=False`) if X11 detected |
| **Status** | ⚠️ Wayland-only tested |

---

## 5. IM Restart Behavior

### 5.1 Fcitx5 Restart (Manual or Crash)

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Keyboard disappears, doesn't come back |
| **Root Cause** | Engine addon destroyed; UI orphaned |
| **Severity** | P1 |
| **Detection** | `journalctl --user -u fcitx5` shows restart |
| **Current Mitigation** | UI's reconnect loop will find new socket when engine restarts |
| **Recovery Strategy** | Socket path is deterministic; new engine creates same socket |
| **Status** | ✅ UI auto-reconnects |

### 5.2 Input Method Switch

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Keyboard stays visible when switching to keyboard IM |
| **Root Cause** | Deactivate not called when switching within Fcitx5 |
| **Severity** | P2 |
| **Detection** | Keyboard visible but `isOurs` check fails |
| **Current Mitigation** | `shouldShowKeyboard()` checks if Magic Keyboard is active IM (lines 150-161) |
| **Recovery Strategy** | `handleFocusIn()` hides if `show==0` (lines 190-192) |
| **Status** | ✅ Implemented |

### 5.3 Session Logout/Lock

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Engine/UI don't clean up properly |
| **Root Cause** | SIGTERM not delivered; socket file left behind |
| **Severity** | P3 |
| **Detection** | Stale socket on next login |
| **Current Mitigation** | `unlink(path.c_str())` before bind (line 661); also in destructor (line 753) |
| **Recovery Strategy** | `unlink` before `bind` handles stale files |
| **Status** | ✅ Implemented |

### 5.4 Hot Reload of Addon

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Configuration changes not applied |
| **Root Cause** | Fcitx5 doesn't reload addons without restart |
| **Severity** | P3 |
| **Detection** | Changed config, no effect |
| **Current Mitigation** | `reloadConfig()` is empty (line 100) |
| **Recovery Strategy** | Implement config watching (future) or document restart requirement |
| **Status** | ⚠️ Not implemented |

---

## 6. IPC Failures

### 6.1 Socket Permission Denied

| Aspect | Details |
|--------|---------|
| **Failure Mode** | UI cannot connect; socket exists but wrong permissions |
| **Root Cause** | Socket created by root, UI runs as user |
| **Severity** | P0 |
| **Detection** | `Socket error: Permission denied` |
| **Current Mitigation** | Both run as same user (deck) |
| **Recovery Strategy** | Add `fchmod(serverFd_, 0600)` after bind |
| **Status** | ⚠️ Not explicitly set |

### 6.2 Socket Buffer Overflow

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Write blocks or fails; messages lost |
| **Root Cause** | UI not reading fast enough; high swipe rate |
| **Severity** | P2 |
| **Detection** | `Send failed to fd X: Resource temporarily unavailable` |
| **Current Mitigation** | Non-blocking socket (SOCK_NONBLOCK line 663) |
| **Recovery Strategy** | Drop message rather than block; log warning |
| **Status** | ✅ Non-blocking, drops cleanly |

### 6.3 Malformed JSON

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Message parsing fails; action not taken |
| **Root Cause** | Bug in JSON construction or buffer corruption |
| **Severity** | P2 |
| **Detection** | No action taken; parse falls through |
| **Current Mitigation** | String-contains parsing is forgiving |
| **Recovery Strategy** | Current approach is resilient to extra fields; consider dedicated parser for v0.3 |
| **Status** | ✅ Tolerant parsing |

---

## 7. Platform-Specific Failures

### 7.1 Wayland Layer-Shell Assumption

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Keyboard doesn't appear on top or in correct layer |
| **Root Cause** | Using wlr-layer-shell (not available on KDE) |
| **Severity** | P0 |
| **Detection** | Keyboard hidden behind windows |
| **Current Mitigation** | Uses `Qt::WindowStaysOnTopHint` instead |
| **Recovery Strategy** | Documented in ARCHITECTURE.md (lines 36-38) |
| **Status** | ✅ KDE-compatible |

### 7.2 X11 vs Wayland Focus Behavior

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Focus behavior differs between X11 and Wayland |
| **Root Cause** | Different focus models; X11 allows more hacks |
| **Severity** | P2 |
| **Detection** | Works on Wayland, fails on X11 (or vice versa) |
| **Current Mitigation** | Testing on both |
| **Recovery Strategy** | Add X11-specific WM hints in UI main.cpp |
| **Status** | ⚠️ X11 not fully tested |

### 7.3 SteamOS Read-Only Filesystem

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Installation fails |
| **Root Cause** | `/usr` is read-only by default on SteamOS |
| **Severity** | P0 (install-time) |
| **Detection** | `Read-only file system` errors |
| **Current Mitigation** | Document `steamos-readonly disable` |
| **Recovery Strategy** | Install to `/usr/local/` or `/home/deck/.local/` |
| **Status** | ✅ Documented |

---

## 8. Resource Failures

### 8.1 Dictionary Not Found

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Swipe typing returns no candidates |
| **Root Cause** | Dictionary files missing from search paths |
| **Severity** | P1 |
| **Detection** | Log `Dictionary not found` (line 802) |
| **Current Mitigation** | Multiple search paths checked (lines 788-798) |
| **Recovery Strategy** | Graceful degradation - swipe still works, just no words |
| **Status** | ✅ Degradation handled |

### 8.2 Layout File Missing

| Aspect | Details |
|--------|---------|
| **Failure Mode** | Swipe path can't be mapped to keys |
| **Root Cause** | qwerty.json not found |
| **Severity** | P1 |
| **Detection** | Log `Failed to find layout: qwerty` (line 337) |
| **Current Mitigation** | Multiple search paths (lines 322-325) |
| **Recovery Strategy** | Tap typing still works; only key-to-swipe mapping fails |
| **Status** | ✅ Partial degradation |

### 8.3 Memory Exhaustion

| Aspect | Details |
|--------|---------|
| **Failure Mode** | OOM killer terminates UI or engine |
| **Root Cause** | Large dictionary, memory leak, system pressure |
| **Severity** | P0 |
| **Detection** | Process killed, `dmesg` shows OOM |
| **Current Mitigation** | None |
| **Recovery Strategy** | Auto-respawn; consider lazy dictionary loading |
| **Status** | ⚠️ No protection |

---

## 9. Recovery Strategies Summary

### Automatic Recovery

| Component | Failure | Recovery |
|-----------|---------|----------|
| UI Process | Crash | Engine respawns on next focus via `ensureUIRunning()` |
| Socket Connection | Disconnect | UI retries every 1s via `scheduleReconnect()` |
| Focus State | Stale | Watchdog corrects every 500ms |
| Candidate State | Orphaned | `reset()` clears on context change |

### Manual Recovery

| Failure | User Action |
|---------|-------------|
| Keyboard won't show | Run `~/.local/share/applications/magickeyboard-show.desktop` |
| Keyboard stuck visible | Run `~/.local/share/applications/magickeyboard-hide.desktop` |
| Complete freeze | `systemctl --user restart fcitx5` |
| Persistent issues | Check `journalctl --user -u fcitx5 -f` for errors |

---

## 10. Testing Checklist

### Focus Edge Cases
- [ ] Rapid-click between two text fields
- [ ] Click text field in password dialog
- [ ] Focus field behind popup
- [ ] Switch windows during typing
- [ ] Lock screen and unlock

### Race Conditions
- [ ] Start UI before engine
- [ ] Start engine before Wayland
- [ ] Kill engine during active typing
- [ ] Kill UI during active swipe

### Component Failures
- [ ] Delete dictionary file, verify graceful degradation
- [ ] Delete layout file, verify tap typing works
- [ ] Fill /tmp, verify socket fallback
- [ ] SIGKILL engine, verify UI reconnects

### Platform Testing
- [ ] KDE Wayland (primary target)
- [ ] KDE X11
- [ ] After SteamOS update (check installation survives)
- [ ] After suspend/resume

---

## 11. Recommended Improvements

### High Priority (P0/P1 fixes)

1. **Debounce hide on FocusOut** - Prevent flicker when focus changes rapidly
2. **Unify IC resolution** - Use consistent `getActiveIC()` helper everywhere
3. **UI disconnect indicator** - Show "Reconnecting..." when engine unavailable
4. **Max reconnect limit** - Don't spam logs forever

### Medium Priority (P2 hardening)

5. **Socket permissions** - Explicit `fchmod(0600)` after bind
6. **X11 WM hints** - Add `InputHint=False` for X11 sessions
7. **Config reload** - Implement `reloadConfig()` for hot changes
8. **GPU context recovery** - Force window recreate on suspicious state

### Low Priority (P3 polish)

9. **Structured logging** - JSON logs for automated analysis
10. **Health endpoint** - Allow `magickeyboardctl status` to query state

---

## Revision History

| Date | Change |
|------|--------|
| 2025-12-29 | Initial failure matrix document |
