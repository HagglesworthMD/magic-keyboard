# Magic Keyboard — Master Implementation Plan

**Authoritative Implementation Tracking Document**  
**Created:** 2025-12-29  
**Status:** Active Development

---

## Executive Summary

This document tracks the implementation status of all Magic Keyboard agents and their deliverables. It serves as the single source of truth for development progress, dependencies, and completion criteria.

---

## Agent Execution Order (Authoritative)

The following order is **mandatory** and reflects dependency chains and risk mitigation:

| # | Agent | Design Spec | Status | Priority |
|---|-------|-------------|--------|----------|
| 1 | [IME Core Architect](#1-ime-core-architect) | `ARCHITECTURE.md` | 🟡 Partial | Foundation |
| 2 | [Focus & Visibility Control](#2-focus--visibility-control) | `.agent/FOCUS_VISIBILITY_CONTROL.md` | 🟢 Complete | Critical |
| 3 | [Keyboard UI Agent](#3-keyboard-ui-agent) | `docs/KEYBOARD_UI_AGENT.md` | 🟡 Partial | Critical |
| 4 | [Magic Trackpad Gesture Agent](#4-magic-trackpad-gesture-agent) | `docs/GESTURE_AGENT.md` | 🔴 Not Started | High |
| 5 | [Swipe Typing Engine](#5-swipe-typing-engine) | `docs/SWIPE_ENGINE_SPEC.md` | 🟡 Partial | High |
| 6 | [Candidate Bar Agent](#6-candidate-bar-agent) | `.agent/CANDIDATE_BAR_SELECTION.md` | 🟡 Partial | High |
| 7 | [Clipboard & Shortcut Agent](#7-clipboard-shortcut-agent) | `.agent/CLIPBOARD_SHORTCUT_AGENT.md` | 🟢 Complete | Medium |
| 8 | [Settings Agent](#8-settings-agent) | `docs/SETTINGS_PERSISTENCE_AGENT.md` | 🔴 Not Started | Medium |
| 9 | [Packaging Agent](#9-packaging-agent) | `docs/DEPLOYMENT_AGENT.md` | 🔴 Not Started | Medium |
| 10 | [QA Agent](#10-qa-agent) | `docs/FAILURE_MATRIX.md` | 📋 Analysis Only | Ongoing |

**Legend:**
- 🟢 Complete — All deliverables implemented and tested
- 🟡 Partial — Core functionality exists, needs enhancement
- 🔴 Not Started — Design spec exists, no implementation
- 📋 Analysis Only — Reference document, not code deliverable

---

## Dependency Graph

```
┌───────────────────────────────────────────────────────────────────┐
│                       DEPENDENCY HIERARCHY                         │
└───────────────────────────────────────────────────────────────────┘

                    ┌─────────────────────┐
                    │  1. IME Core        │  ← Foundation (all others depend)
                    │     Architect       │
                    └──────────┬──────────┘
                               │
              ┌────────────────┼────────────────┐
              │                │                │
              ▼                ▼                ▼
    ┌─────────────────┐  ┌───────────────┐  ┌───────────────────┐
    │ 2. Focus &      │  │ 3. Keyboard   │  │ 8. Settings       │
    │    Visibility   │  │    UI Agent   │  │    Agent          │
    └────────┬────────┘  └───────┬───────┘  └───────────────────┘
             │                   │
             │    ┌──────────────┤
             │    │              │
             ▼    ▼              ▼
    ┌─────────────────────┐  ┌───────────────────┐
    │ 4. Trackpad Gesture │  │ 7. Clipboard &    │
    │    Agent            │  │    Shortcut       │
    └──────────┬──────────┘  └───────────────────┘
               │
               ▼
    ┌─────────────────────┐
    │ 5. Swipe Typing     │
    │    Engine           │
    └──────────┬──────────┘
               │
               ▼
    ┌─────────────────────┐
    │ 6. Candidate Bar    │
    │    Agent            │
    └──────────┬──────────┘
               │
               ▼
    ┌─────────────────────┐     ┌─────────────────────┐
    │ 9. Packaging Agent  │────▶│ 10. QA Agent        │
    └─────────────────────┘     └─────────────────────┘
```

---

## 1. IME Core Architect

**Design Spec:** `ARCHITECTURE.md`  
**Status:** 🟡 Partial  
**Implementation:** `src/engine/magickeyboard.cpp` (~965 lines)

### Deliverables

| Deliverable | Status | Notes |
|-------------|--------|-------|
| Fcitx5 addon scaffold | ✅ Done | `MagicKeyboardEngine` class |
| InputContext tracking | ✅ Done | `currentIC_` member |
| Focus in/out handlers | ✅ Done | Basic implementation |
| Unix socket IPC server | ✅ Done | Multi-client support |
| UI process lifecycle | ✅ Done | `launchUI()`, `ensureUIRunning()` |
| Text commit to app | ✅ Done | `handleKeyPress()` |
| Basic show/hide | ✅ Done | Socket messages |
| Watchdog timer | ✅ Done | `startWatchdog()` |
| Shutdown safety | ✅ Done | `shuttingDown_` atomic flag |

### Pending for This Agent

| Task | Priority | Blocked By |
|------|----------|------------|
| n/a — Complete for v0.1 scope | — | — |

---

## 2. Focus & Visibility Control

**Design Spec:** `.agent/FOCUS_VISIBILITY_CONTROL.md`  
**Workflow:** `.agent/workflows/implement-focus-control.md`  
**Status:** 🟢 Complete  
**Completed:** 2025-12-29

### Deliverables

| Deliverable | Status | Notes |
|-------------|--------|-------|
| `VisibilityState` enum | ✅ Done | `Hidden`, `PendingShow`, `Visible`, `PendingHide` |
| State machine transitions | ✅ Done | Replaced boolean `keyboardVisible_` |
| Debounced `handleFocusIn` | ✅ Done | 50ms delay before show |
| Debounced `handleFocusOut` | ✅ Done | 100ms delay before hide |
| `scheduleDebounce()` helper | ✅ Done | Timer-based state transitions |
| `cancelDebounce()` helper | ✅ Done | Cancel pending transitions |
| `executeShow()` / `executeHide()` | ✅ Done | Final visibility actions |
| Watchdog integration | ✅ Done | Uses state machine |
| Capability checks | ✅ Done | Password, NoOnScreenKeyboard, Sensitive |

### Operational Hardening (Required for Stable Testing)

| Deliverable | Status | Notes |
|-------------|--------|-------|
| CMake install path fix | ✅ Done | Uses GNUInstallDirs, configure-time guard |
| Single-client enforcement | ✅ Done | Engine closes existing clients on new connect |
| UI exponential backoff | ✅ Done | 100ms→5s cap, prevents reconnect storm |
| Logging spam fix | ✅ Done | "UI connected" once per session |
| systemd service disabled | ✅ Done | Engine-spawned UI only (no dual authority) |
| KDE Launcher (.desktop) | ✅ Done | Native Start Menu integration via `magickeyboardctl` |
| Standard Prefix (/usr) | ✅ Done | Fixed "program not found" errors in KDE |

### Implementation Notes

- `ReadOnly` flag does not exist in Fcitx5 API; using `NoOnScreenKeyboard` and `Sensitive` instead
- Debounce timers: 50ms show, 100ms hide, 500ms watchdog
- State machine prevents flicker from rapid focus changes
- Destructor properly cleans up debounce timer
- Install paths: Standardized to `/usr` for reliable system integration.
- Desktop integration: `/usr/share/applications/magickeyboard.desktop`

### Verification Criteria

- [ ] Open Kate, click text area → keyboard shows (after ~50ms)
- [ ] Click Kate sidebar → keyboard hides (after ~100ms)
- [ ] Rapid click between text and sidebar → **no flicker**
- [ ] Tab between fields → keyboard stays visible
- [ ] Focus password field → keyboard hides
- [ ] Focus sensitive/private field → keyboard hides
- [ ] No `/addon` or `/inputmethod` at filesystem root
- [ ] No connection storm (single "UI connected" log)

---

## 3. Keyboard UI Agent

**Design Spec:** `docs/KEYBOARD_UI_AGENT.md`  
**Status:** 🟡 Partial  
**Implementation:** `src/ui/`

### Deliverables

| Deliverable | Status | Notes |
|-------------|--------|-------|
| QML keyboard layout | ✅ Done | Basic QWERTY |
| Click-to-type | ✅ Done | Key press sends IPC |
| Non-focus window | ✅ Done | `Qt::WindowDoesNotAcceptFocus` |
| Socket client | ✅ Done | `KeyboardBridge` class |
| Show/hide handling | ✅ Done | Visibility message handling |
| Key sizing per spec | 🔴 TODO | 72×52px minimum |
| Key hover states | 🔴 TODO | Visual feedback |
| Candidate bar row | 🟡 Partial | Basic implementation |
| Swipe trail overlay | 🔴 TODO | Real-time path visualization |
| Responsive scaling | 🔴 TODO | Steam Deck 1280×800 optimization |
| Shift key toggle | ✅ Done | State management |
| Number/symbol layout | 🔴 TODO | Mode switching |
| Shortcut keys row | 🔴 TODO | Copy/Paste/Cut/SelectAll |

### Pending for This Agent

| Task | Priority | Blocked By |
|------|----------|------------|
| Implement hover states | High | None |
| Key sizing per spec (72×52px) | High | None |
| Swipe trail overlay | High | Agent #4 |
| Shortcut keys row | Medium | Agent #7 |
| Number/symbol layout | Medium | None |

---

## 4. Magic Trackpad Gesture Agent

**Design Spec:** `docs/GESTURE_AGENT.md`  
**Status:** 🔴 Not Started  
**Implementation:** `src/gesture/` (scaffold only)

### Deliverables

| Deliverable | Status | Notes |
|-------------|--------|-------|
| Gesture state machine | 🔴 TODO | `Idle` → `TapPending` → `Swiping` → `Complete` |
| Tap vs swipe differentiation | 🔴 TODO | Time + distance thresholds |
| Deadzone logic | 🔴 TODO | 12px minimum movement |
| Pointer sampling | 🔴 TODO | 120Hz target |
| Jitter smoothing (EMA) | 🔴 TODO | α=0.35 filter |
| Path point collection | 🔴 TODO | Array of (x,y,timestamp) |
| Gesture start conditions | 🔴 TODO | Mouse down inside key area |
| Gesture termination | 🔴 TODO | Mouse up or timeout |
| Conflict avoidance | 🔴 TODO | Short tap = key press |
| Path→Key mapping call | 🔴 TODO | Invoke swipe engine |

### Pending for This Agent

| Task | Priority | Blocked By |
|------|----------|------------|
| Full implementation | High | Agent #3 (UI hover integration) |

---

## 5. Swipe Typing Engine

**Design Spec:** `docs/SWIPE_ENGINE_SPEC.md`  
**Status:** 🟡 Partial  
**Implementation:** `src/engine/magickeyboard.cpp` (integrated)

### Deliverables

| Deliverable | Status | Notes |
|-------------|--------|-------|
| Layout loading (JSON) | ✅ Done | `loadLayout()` |
| Key centroid model | ✅ Done | `KeyRect` struct |
| Path→key sequence mapping | ✅ Done | `mapPathToSequence()` |
| Hysteresis filtering | ✅ Done | Bounce removal |
| Dictionary loading | ✅ Done | `loadDictionary()` |
| Shortlist generation | ✅ Done | `getShortlist()` |
| Candidate scoring | ✅ Done | `scoreCandidate()` |
| Levenshtein distance | ✅ Done | `levenshtein()` |
| Candidate generation | ✅ Done | `generateCandidates()` |
| Spatial distance scoring | 🟡 Basic | Could be enhanced |
| Context awareness | 🔴 TODO | Previous word consideration |
| Bigram scoring | 🔴 TODO | Letter pair frequency |

### Pending for This Agent

| Task | Priority | Blocked By |
|------|----------|------------|
| Spatial distance enhancement | Medium | None |
| Bigram scoring | Low | None |
| Context awareness | Low | None |

---

## 6. Candidate Bar Agent

**Design Spec:** `.agent/CANDIDATE_BAR_SELECTION.md`  
**Status:** 🟡 Partial  
**Implementation:** Integrated in engine + UI

### Deliverables

| Deliverable | Status | Notes |
|-------------|--------|-------|
| Candidate list display | ✅ Done | IPC to UI |
| Candidate selection | ✅ Done | Click to commit |
| Top candidate auto-highlight | 🔴 TODO | Visual emphasis |
| Hover preview | 🔴 TODO | Show before click |
| Backspace interaction | 🟡 Partial | Clears candidates |
| Auto-commit timeout | 🔴 TODO | 2s without interaction |
| Learning signals | 🔴 TODO | Track selections |
| Candidate ordering | ✅ Done | Score-based |
| Max 5-8 candidates | ✅ Done | Configurable |

### Pending for This Agent

| Task | Priority | Blocked By |
|------|----------|------------|
| Auto-commit timeout | Medium | None |
| Hover preview | Medium | Agent #3 |
| Learning signals | Low | Agent #8 |

---

## 7. Clipboard & Shortcut Agent

**Design Spec:** `.agent/CLIPBOARD_SHORTCUT_AGENT.md`  
**Workflow:** `.agent/workflows/implement-clipboard-shortcut.md`  
**Status:** 🟢 Complete  
**Completed:** 2025-12-29

### Deliverables

| Deliverable | Status | Notes |
|-------------|--------|-------|
| `handleShortcutAction()` | ✅ Done | Core dispatch function |
| `isTerminal()` detector | ✅ Done | Terminal emulator list |
| Action message parsing | ✅ Done | `{"type":"action","action":"paste"}` |
| Key forwarding via `forwardKey()` | ✅ Done | Wayland-safe |
| Password field blocking | ✅ Done | Block copy/cut |
| Read-only field blocking | ✅ Done | Block paste/cut |
| Terminal mode shortcuts | ✅ Done | Ctrl+Shift+C/V |
| Logging for shortcuts | ✅ Done | Dispatch trace |
| UI shortcut keys | ✅ Done | Copy/Paste/Cut/SelectAll buttons |
| `sendAction()` bridge slot | ✅ Done | UI→Engine IPC |

### Implementation Notes

- Fixed KDE Plasma launcher by standardizing install prefix to `/usr`
- Added fallback logic for `stod` in layout loader (Operational Hardening)
- Verified with `magickeyboardctl` and manual socket messages

---

## 8. Settings Agent

**Design Spec:** `docs/SETTINGS_PERSISTENCE_AGENT.md`  
**Status:** 🔴 Not Started  
**Implementation:** Not started

### Deliverables

| Deliverable | Status | Notes |
|-------------|--------|-------|
| `ConfigManager` class | 🔴 TODO | Load/save/merge logic |
| XDG path resolution | 🔴 TODO | `~/.config/magic-keyboard/` |
| Default config template | 🔴 TODO | `config.json` |
| Layered config loading | 🔴 TODO | System → User → Env → Runtime |
| Atomic write (temp+rename) | 🔴 TODO | No corruption on crash |
| Validation & clamping | 🔴 TODO | Range enforcement |
| Environment overrides | 🔴 TODO | `MAGIC_KEYBOARD_*` vars |
| Learning persistence | 🔴 TODO | `learning.json` |
| User dictionary | 🔴 TODO | `user-dictionary.txt` |
| Frequency adjustments | 🔴 TODO | `word-frequencies.tsv` |
| Reset mechanisms | 🔴 TODO | Session/Config/Learning/Full |
| IPC config commands | 🔴 TODO | get_config, set_config |
| Settings UI panel | 🔴 TODO | QML settings screen |

### Pending for This Agent

| Task | Priority | Blocked By |
|------|----------|------------|
| ConfigManager core | Medium | None |
| Atomic writes | Medium | None |
| Settings UI | Low | Agent #3 |

---

## 9. Packaging Agent

**Design Spec:** `docs/DEPLOYMENT_AGENT.md`  
**Status:** 🔴 Not Started  
**Implementation:** `packaging/` (scaffold only)

### Deliverables

| Deliverable | Status | Notes |
|-------------|--------|-------|
| CMake install targets | 🟡 Partial | Basic targets exist |
| `/usr/local/` install path | 🔴 TODO | SteamOS-safe |
| Fcitx5 addon discovery | 🟡 Partial | Config files exist |
| systemd user service | 🟡 Partial | Template exists |
| KDE autostart fallback | 🔴 TODO | `.desktop` file |
| Environment setup script | 🔴 TODO | `fcitx5.sh` |
| Flatpak overrides doc | 🔴 TODO | For Firefox/Chrome |
| PKGBUILD | 🟡 Partial | Template exists |
| Verification script | 🔴 TODO | `magic-keyboard-verify` |
| Post-install instructions | 🔴 TODO | User documentation |
| Uninstall procedure | 🔴 TODO | Clean removal |

### Pending for This Agent

| Task | Priority | Blocked By |
|------|----------|------------|
| Full PKGBUILD | Medium | Feature complete |
| Verification script | Medium | None |
| Documentation | Medium | None |

---

## 10. QA Agent

**Design Spec:** `docs/FAILURE_MATRIX.md`  
**Status:** 📋 Analysis Only  

### Purpose

The QA Agent provides:
- Failure mode analysis
- Recovery strategies
- Testing checklists
- Improvement recommendations

### Key Sections

| Section | Content |
|---------|---------|
| Focus Edge Cases | Flicker loops, password detection, IC nulls |
| Race Conditions | Socket writes, startup ordering, candidate state |
| Startup/Shutdown | Wayland readiness, orphaned processes |
| UI Crash Recovery | QML load, segfault, GPU context |
| IM Restart | Fcitx5 restart, session logout |
| IPC Failures | Permissions, buffer overflow, malformed JSON |
| Platform Failures | Wayland vs X11, SteamOS read-only |
| Resource Failures | Dictionary missing, OOM |

### Recommended Improvements (from Failure Matrix)

**High Priority (P0/P1):**
1. ✅ Debounce hide on FocusOut → Agent #2
2. ✅ Unify IC resolution → Agent #1
3. 🔴 UI disconnect indicator
4. 🔴 Max reconnect limit

**Medium Priority (P2):**
5. 🔴 Socket permissions `fchmod(0600)`
6. 🔴 X11 WM hints
7. 🔴 Config reload
8. 🔴 GPU context recovery

---

## Version Milestones

### v0.1: Foundation ✅ (Current)

| Feature | Status |
|---------|--------|
| Fcitx5 engine scaffold | ✅ |
| Socket IPC | ✅ |
| QML keyboard | ✅ |
| Click-to-type | ✅ |
| Basic show/hide | ✅ |
| Letters + backspace + enter + shift | ✅ |

### v0.2: Swipe Typing 🟡 (In Progress)

| Feature | Status |
|---------|--------|
| Path→key mapping | ✅ |
| Dictionary loading | ✅ |
| Candidate generation | ✅ |
| Candidate bar UI | 🟡 Partial |
| Swipe gesture detection | 🔴 TODO |
| Swipe trail visual | 🔴 TODO |

### v0.3: Polish & Deploy 🔴 (Planned)

| Feature | Status |
|---------|--------|
| Focus state machine | 🔴 TODO |
| Copy/Paste keys | 🔴 TODO |
| Terminal mode | 🔴 TODO |
| Settings persistence | 🔴 TODO |
| PKGBUILD complete | 🔴 TODO |
| Install documentation | 🔴 TODO |

### v0.4: Learning & Refinement 🔴 (Future)

| Feature | Status |
|---------|--------|
| Word frequency learning | 🔴 TODO |
| User dictionary | 🔴 TODO |
| Settings UI | 🔴 TODO |
| Fcitx5 config integration | 🔴 TODO |

---

## Quick Reference: File Locations

### Design Specs

| Agent | Location |
|-------|----------|
| IME Core | `ARCHITECTURE.md` |
| Focus & Visibility | `.agent/FOCUS_VISIBILITY_CONTROL.md` |
| Keyboard UI | `docs/KEYBOARD_UI_AGENT.md` |
| Gesture | `docs/GESTURE_AGENT.md` |
| Swipe Engine | `docs/SWIPE_ENGINE_SPEC.md` |
| Candidate Bar | `.agent/CANDIDATE_BAR_SELECTION.md` |
| Clipboard & Shortcut | `.agent/CLIPBOARD_SHORTCUT_AGENT.md` |
| Settings | `docs/SETTINGS_PERSISTENCE_AGENT.md` |
| Packaging | `docs/DEPLOYMENT_AGENT.md` |
| QA / Failure Matrix | `docs/FAILURE_MATRIX.md` |

### Implementation

| Component | Location |
|-----------|----------|
| Engine | `src/engine/` |
| UI | `src/ui/` |
| Gesture | `src/gesture/` |
| IPC Protocol | `src/ipc/` |
| Layouts | `data/layouts/` |
| Packaging | `packaging/` |

### Workflows

| Workflow | Location |
|----------|----------|
| Focus Control | `.agent/workflows/implement-focus-control.md` |

---

## Next Actions

Based on the dependency graph and current status, the **recommended next implementation order** is:

1. **Agent #2: Focus & Visibility Control** — Use existing workflow
2. **Agent #3: Keyboard UI enhancements** — Key sizing, hover states
3. **Agent #4: Trackpad Gesture** — Swipe detection
4. **Agent #7: Clipboard & Shortcut** — Copy/Paste functionality
5. **Agent #8: Settings** — Persistence layer
6. **Agent #9: Packaging** — Distribution

---

## Revision History

| Date | Change |
|------|--------|
| 2025-12-29 | Initial master implementation plan created |

---

*This document should be updated whenever an agent implementation progresses.*
