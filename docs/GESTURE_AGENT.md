# Magic Trackpad Gesture Agent (Swipe Specialist)

**Authoritative Technical Design Document**

This document defines the gesture detection and processing system for swipe typing via trackpad pointer click-drag input. The Gesture Agent is the single source of truth for all pointer gesture logic.

---

## Role & Authority

**The Gesture Agent owns all pointer gesture logic.**

- iOS-style swipe typing via click-drag (not touch)
- Distinction between tap (click-release) and swipe (click-hold-drag-release)
- No direct text output—only gesture state and path data

**Explicitly Separated From:**
- Touch typing (button clicks) → handled by standard MouseArea
- Candidate selection → handled by Candidate Agent
- Text commitment → handled by Engine
- UI rendering → handled by QML layer

---

## Hard Constraints

### 1. Trackpad ≠ Touchscreen

Magic Trackpad is a **pointer device**, not a multitouch surface. This means:

| Touchscreen | Trackpad Pointer |
|-------------|------------------|
| Finger touches absolute position | Cursor at relative delta position |
| Fat finger occlusion | Single precise click point |
| Multitouch gestures | Click + drag only |
| Variable contact size | Binary click (yes/no) |
| Pressure/force data | None (or button down) |

**Rule:** Design for cursor precision, not finger sloppiness.

### 2. Jitter & Noise

Trackpad input has inherent noise:

| Source | Magnitude | Mitigation |
|--------|-----------|------------|
| Hardware sensor noise | 1-3 px | EMA smoothing |
| Tremor at gesture start | 3-8 px | Deadzone |
| Inertial overshoot | 5-15 px | Velocity damping |
| Trackpad edge behavior | Variable | Boundary clamp |

### 3. Wayland Security Model

- No global pointer observation
- Pointer events arrive only when window has implicit grab (button down)
- Cannot distinguish "hover over text field" from window interaction

---

## Gesture State Machine

```
                          ┌─────────────────────────────────────────┐
                          │                                         │
                          ▼                                         │
┌──────────┐   mouseDown  ┌──────────────┐                          │
│   IDLE   │─────────────▶│  TAP_PENDING │                          │
└──────────┘              └──────────────┘                          │
     ▲                    │           │                             │
     │                    │ dist > DZ │ mouseUp                     │
     │                    │ AND       │ (before DZ)                 │
     │ mouseUp            │ dt > TT   │                             │
     │ (anywhere)         │           ▼                             │
     │                    │    ┌────────────┐                       │
     │                    │    │   TAPPED   │───────────────────────┘
     │                    │    └────────────┘ emit: tap(key)
     │                    │
     │                    ▼
     │              ┌─────────────┐  mouseMove  ┌────────────────┐
     └──────────────│   SWIPING   │◀───────────▶│ SAMPLE + SMOOTH│
                    └─────────────┘             └────────────────┘
                          │
                          │ mouseUp
                          ▼
                    ┌─────────────┐
                    │  COMPLETED  │
                    └─────────────┘
                          │
                          │ emit: swipe_path(points[])
                          ▼
                    ┌─────────────┐
                    │    IDLE     │
                    └─────────────┘
```

### State Definitions

| State | Description | Entry Condition | Exit Condition |
|-------|-------------|-----------------|----------------|
| `IDLE` | Waiting for input | Initial / gesture complete | mouseDown |
| `TAP_PENDING` | Button down, awaiting intent | mouseDown | dist > DZ AND dt > TT → SWIPING; mouseUp → TAPPED |
| `SWIPING` | Active swipe, sampling path | Deadzone broken | mouseUp → COMPLETED |
| `COMPLETED` | Swipe done, emitting path | mouseUp during SWIPING | Immediate → IDLE |
| `TAPPED` | Click detected, not swipe | mouseUp during TAP_PENDING | Immediate → IDLE |

### Key Parameters

| Parameter | Symbol | Value | Rationale |
|-----------|--------|-------|-----------|
| Deadzone radius | `DZ` | **10 px** | Absorbs tremor without feeling sluggish |
| Time threshold | `TT` | **35 ms** | Prevents ultra-fast flicks from becoming swipes |
| Smoothing factor | `α` | **0.40** | Balance between responsiveness and jitter rejection |
| Resample distance | `RD` | **7 px** | Uniform path density for consistent key mapping |
| Stationary timeout | `ST` | **250 ms** | Detect pause → implicit gesture end (optional) |

---

## Deadzone Logic

### Purpose

Prevent accidental swipe initiation from:
1. Micro-tremor at button press
2. Slight movement during click attempt
3. Trackpad edge sensitivity

### Implementation

```cpp
// On mouseDown:
startPos = currentPos;
startTime = now();

// On mouseMove (while TAP_PENDING):
double dx = currentPos.x - startPos.x;
double dy = currentPos.y - startPos.y;
double dist = sqrt(dx*dx + dy*dy);
double dt = now() - startTime;

if (dist > DEADZONE && dt > TIME_THRESHOLD) {
    transitionTo(SWIPING);
}
```

### Deadzone Shape

**Circular**, not rectangular. Diagonal movement should not be penalized.

```
     ─────────────
    ╱             ╲
   ╱     START     ╲
  │        ⬤        │  ← radius = 10px
   ╲               ╱
    ╲─────────────╱
```

### Adaptive Deadzone (Future v0.4)

Consider scaling DZ by:
- Key size: smaller keys → larger DZ
- Recent input velocity: fast hands → larger DZ
- User preference setting

---

## Pointer Sampling

### Sampling Strategy

**Event-driven with distance gating:**

1. Every `mouseMoveEvent` is received (~120Hz on smooth trackpad)
2. Apply EMA smoothing immediately
3. Only emit sample if distance from last sample > `RD`

This produces uniform path density regardless of pointer speed.

### Coordinate Spaces

```
┌─────────────────────────────────────────────────┐
│                  WINDOW SPACE                    │
│  (0,0)────────────────────────────────────────  │
│    │                                             │
│    │  ┌─────────────────────────────────────┐   │
│    │  │        LAYOUT SPACE (keysContainer) │   │
│    │  │   ┌───┬───┬───┬───┬───┐             │   │
│    │  │   │ Q │ W │ E │ R │ T │ ...         │   │
│    │  │   └───┴───┴───┴───┴───┘             │   │
│    │  └─────────────────────────────────────┘   │
│    │                                             │
└────┴────────────────────────────────────────────┘
```

**Trail rendering**: Use window space (wx, wy) for visual consistency.
**Key hit-testing**: Use layout space (x, y) for geometry accuracy.

### Sample Structure

```cpp
struct PathPoint {
    double wx, wy;  // Window space (for rendering)
    double x, y;    // Layout space (for key mapping)
    uint64_t timestamp;  // For velocity calculation
};
```

---

## Jitter Smoothing

### Exponential Moving Average (EMA)

```cpp
smoothedPos.x = α * rawPos.x + (1 - α) * prevSmoothedPos.x;
smoothedPos.y = α * rawPos.y + (1 - α) * prevSmoothedPos.y;
```

| α Value | Characteristic |
|---------|----------------|
| 1.0 | No smoothing (raw input) |
| 0.5 | Moderate smoothing |
| 0.4 | **Selected**: Good balance |
| 0.2 | Heavy smoothing (laggy) |

### Why Not Moving Average?

Moving average (mean of last N samples) introduces latency proportional to N.
EMA responds immediately to direction changes while still dampening noise.

### Visual vs. Logic Paths

- **Visual trail**: Use smoothed coordinates for aesthetic appeal
- **Key mapping**: Use smoothed coordinates for consistent results
- **Raw coordinates**: Never used directly (too noisy)

---

## Gesture Termination

### Primary: Mouse Button Up

The canonical gesture end is `mouseReleased()`. This is reliable and unambiguous.

### Secondary: Stationary Timeout (Optional)

If cursor is stationary for `ST` milliseconds during SWIPING:
- Implicit gesture complete
- Useful for dwell-to-select scenarios

**Currently disabled** in v0.2 to avoid unexpected behavior.

### Tertiary: Boundary Exit

If cursor exits keyboard bounds during swipe:
1. **Do NOT cancel** the gesture
2. Clamp coordinates to nearest edge
3. Continue sampling at edge
4. Complete normally on mouseUp

Rationale: Users overshooting the keyboard shouldn't lose their word.

---

## Conflict Avoidance: Tap vs. Swipe

### The Core Problem

```
User Intention      Input Events           Classification
─────────────       ──────────────         ──────────────
Click "A"           down, up               TAP
Click "A" (slow)    down, (50ms), up       TAP
Swipe "hello"       down, move..., up      SWIPE
Tiny movement       down, (3px move), up   TAP (within DZ)
Quick flick         down, (25ms), (15px)   TAP (before TT) ← ambiguous
```

### Resolution Rules

**Rule 1: Spatial Dominance**
- `dist > DZ` is necessary for SWIPE
- Small movements are always TAP

**Rule 2: Temporal Gating**  
- `dt > TT` is necessary for SWIPE
- Ultra-fast movements are always TAP (even if large)

**Rule 3: Intent Recovery**
- If TAP_PENDING → TAPPED, emit the key under `startPos`
- If TAP_PENDING → SWIPING, the start key is **not** emitted as a tap

### Edge Case: Borderline Swipe

User moves exactly DZ pixels in exactly TT ms:

| Frame | dist | dt | State |
|-------|------|----|-------|
| 0 | 0 px | 0 ms | TAP_PENDING |
| 1 | 5 px | 17 ms | TAP_PENDING |
| 2 | 10 px | 35 ms | TAP_PENDING (exactly at threshold) |
| 3 | 11 px | 40 ms | → SWIPING |

Thresholds use `>` (strictly greater), not `>=`, to avoid ambiguity.

---

## Algorithm: Path to Key Sequence

### Overview

```
[Raw Path Points] 
       │
       ▼ (EMA smooth)
[Smoothed Path Points]
       │
       ▼ (resample at RD intervals)
[Uniformly Spaced Points]
       │
       ▼ (key hit-test)
[Raw Key Sequence]
       │
       ▼ (hysteresis + dwell filter)
[Clean Key Sequence]
       │
       ▼ (bounce removal)
[Final Key Sequence]
```

### Step 1: Smoothing

Already applied during sampling (see above).

### Step 2: Resampling

Ensure points are uniformly spaced for consistent key hit density.

```cpp
void resamplePath(vector<PathPoint>& path, double targetDist) {
    if (path.size() < 2) return;
    
    vector<PathPoint> resampled;
    resampled.push_back(path[0]);
    double accum = 0;
    
    for (size_t i = 1; i < path.size(); i++) {
        double dx = path[i].x - path[i-1].x;
        double dy = path[i].y - path[i-1].y;
        double seg = sqrt(dx*dx + dy*dy);
        accum += seg;
        
        while (accum >= targetDist) {
            double t = (targetDist - (accum - seg)) / seg;
            PathPoint p;
            p.x = path[i-1].x + t * dx;
            p.y = path[i-1].y + t * dy;
            resampled.push_back(p);
            accum -= targetDist;
        }
    }
    
    resampled.push_back(path.back());
    path = resampled;
}
```

### Step 3: Key Hit-Testing

For each point, find the nearest key:

```cpp
Key* findNearestKey(Point p) {
    Key* best = nullptr;
    double bestDistSq = INFINITY;
    
    // Priority 1: Inside key rect
    for (auto& k : keys) {
        if (k.rect.contains(p)) {
            return &k;
        }
    }
    
    // Priority 2: Nearest center
    for (auto& k : keys) {
        double dx = k.center.x - p.x;
        double dy = k.center.y - p.y;
        double d2 = dx*dx + dy*dy;
        if (d2 < bestDistSq) {
            bestDistSq = d2;
            best = &k;
        }
    }
    
    return best;
}
```

### Step 4: Hysteresis

Prevent oscillation near key boundaries:

```cpp
const double HYSTERESIS_RATIO = 0.72;  // New key must be 28% closer
const double MIN_DISTANCE_GAP = 6.0;   // Minimum absolute difference

bool shouldSwitchKey(Key* current, Key* candidate, Point p) {
    if (candidate->rect.contains(p)) {
        return true;  // Inside rect always wins
    }
    
    double d_cur = distance(current->center, p);
    double d_cand = distance(candidate->center, p);
    
    // Must be significantly closer
    if (d_cand < d_cur * HYSTERESIS_RATIO && 
        (d_cur - d_cand) > MIN_DISTANCE_GAP) {
        return true;
    }
    
    // Consecutive sample confirmation
    // (tracked externally)
    
    return false;
}
```

### Step 5: Bounce Removal

Remove A-B-A patterns where B has very short dwell:

```cpp
vector<string> removeBounces(vector<pair<string, int>>& dwells) {
    vector<string> result;
    
    for (size_t i = 0; i < dwells.size(); i++) {
        // Check A-B-A pattern
        if (i > 0 && i < dwells.size() - 1) {
            if (dwells[i-1].first == dwells[i+1].first && 
                dwells[i].second < 2) {
                continue;  // Skip the "B"
            }
        }
        result.push_back(dwells[i].first);
    }
    
    return result;
}
```

---

## Error Budget Analysis

### Where Errors Enter

| Stage | Error Source | Magnitude | Mitigation |
|-------|--------------|-----------|------------|
| Input | Hardware jitter | 1-3 px | EMA α=0.4 |
| Input | Tremor | 3-8 px | 10px deadzone |
| Sampling | Aliasing | 1-2 px per key | 7px resample |
| Key mapping | Boundary ambiguity | 1 key | Hysteresis |
| Sequence | Duplicate detection | 1 key | Dwell counting |
| Sequence | Bounce artifacts | 1 key | A-B-A filter |

### Acceptable Error Rate

Target: **< 5% key sequence errors** for common words.

Measurement: Compare detected sequence to ground-truth path in test corpus.

---

## Implementation Checklist

### QML (UI Layer)

- [x] MouseArea with hoverEnabled
- [x] startPos/startTime tracking on mouseDown
- [x] Deadzone + time threshold detection
- [x] isSwiping state flag
- [x] EMA smoothing on mouseMoved
- [x] Distance-gated path sampling
- [x] Dual coordinate storage (window + layout)
- [x] Path emission on mouseUp
- [x] Canvas trail visualization
- [x] Trail fade-out timer

### Engine (C++ Layer)

- [x] Path reception via socket
- [x] Layout loading with key geometry
- [x] mapPathToSequence() function
- [x] Inside-rect priority hit-testing
- [x] Nearest-center fallback
- [x] Hysteresis with ratio + gap thresholds
- [x] Dwell counting
- [x] Bounce removal
- [x] Duplicate collapse

---

## Configuration Reference

### Current Settings (v0.2)

```qml
// QML side
readonly property real deadzone: 10          // px
readonly property real timeThreshold: 35     // ms
readonly property real smoothingAlpha: 0.4
readonly property real resampleDist: 7       // px
```

```cpp
// Engine side
const double HYSTERESIS_RATIO = 0.72;
const double MIN_DISTANCE_GAP = 6.0;         // px
const int MIN_CONSECUTIVE_SAMPLES = 2;
const int MIN_DWELL_FOR_BOUNCE = 2;
```

### Tuning Guidelines

| Symptom | Adjustment |
|---------|------------|
| Swipes trigger too easily | Increase deadzone or time threshold |
| Swipes feel sluggish to start | Decrease deadzone or time threshold |
| Trail looks jittery | Decrease smoothingAlpha (more smoothing) |
| Trail lags behind cursor | Increase smoothingAlpha (less smoothing) |
| Missing keys in sequence | Decrease resampleDist |
| Duplicate keys in sequence | Increase hysteresis ratio |
| Bouncing between keys | Increase min dwell for bounce threshold |

---

## Acceptance Criteria

### Functional Requirements

1. **Tap Detection**: Single click-release on a key commits that key's character
2. **Swipe Detection**: Click-drag-release produces a path, not a tap
3. **Deadzone**: Micro-movements during click do not trigger swipe
4. **Smoothing**: Path should not show high-frequency jitter
5. **Key Sequence**: Continuous swipe over Q-W-E-R-T produces "qwert" (or similar)
6. **Trail Visual**: Path is rendered in real-time during swipe
7. **Boundary**: Swiping off keyboard edge does not crash or cancel

### Performance Requirements

1. **Sampling Rate**: ≥ 60 Hz effective (15ms max between samples)
2. **Latency**: Trail rendering < 16ms behind cursor
3. **Path Length**: Support paths with 500+ points
4. **Memory**: Path storage < 50KB per gesture

### Quality Requirements

1. **No False Swipes**: Tapping quickly should never produce a swipe
2. **No Lost Swipes**: Intentional swipes should always be detected
3. **Key Accuracy**: ≥ 95% of swiped keys correctly detected
4. **Boundary Recovery**: Overshooting keyboard should not lose the word

---

## Revision History

| Date | Change |
|------|--------|
| 2024-12-29 | Initial Gesture Agent design document |

---

## Related Documents

- [ARCHITECTURE.md](../ARCHITECTURE.md) - System architecture overview
- [UI_DESIGN.md](UI_DESIGN.md) - Keyboard layout and visual design
- [FOCUS_CONTROL.md](FOCUS_CONTROL.md) - Focus detection agent
- [IME_CORE.md](IME_CORE.md) - Fcitx5 integration layer
