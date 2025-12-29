# Candidate Bar & Selection Agent

**Magic Keyboard - Critical Subsystem Design Document**

## Executive Summary

This document defines the **Candidate Bar UX and commit behavior** for a swipe-enabled on-screen keyboard optimized for pointer hover and click selection via the Apple Magic Trackpad on Steam Deck. The candidate bar is where swiped word suggestions are displayed and selected—critical UX territory that directly impacts typing speed and user satisfaction.

---

## 1. Role & Mission Statement

The Candidate Bar & Selection Agent guarantees:

1. **Clear Visual Hierarchy**: Top candidate is prominently displayed; others accessible but unobtrusive
2. **Effortless Selection**: Hover preview + click commit feels natural and fast
3. **Intelligent Auto-Commit**: Space/Enter/punctuation triggers commit without extra tap
4. **Non-Destructive Backspace**: Backspace clears candidates without deleting committed text
5. **Learning Signals**: Explicit acceptance signals inform future learning (v0.3+)

---

## 2. Architecture Context

```
                                                    Candidate Bar Domain
                                                    ────────────────────
 Swipe Engine              Scoring Engine           │   CANDIDATE BAR    │
 ┌────────────┐            ┌──────────────┐         │  ┌─────────────┐   │
 │ Path→Keys  │──keys────▶│ Score+Rank   │─ranked─▶│  │ 🔵 "hello"  │  ◀── TOP (commit target)
 └────────────┘            └──────────────┘         │  │ ○ "jello"   │   │
                                                    │  │ ○ "hallo"   │   │
                                                    │  └─────────────┘   │
                                                    └─────────▲──────────┘
                                                              │
                                           ┌──────────────────┴──────────────────┐
                                           │                                     │
                                  ┌────────┴────────┐                ┌───────────┴───────────┐
                                  │   HOVER/CLICK   │                │   AUTO-COMMIT KEYS    │
                                  │  (explicit)     │                │  (space/enter/punct)  │
                                  └─────────────────┘                └───────────────────────┘
```

---

## 3. Candidate Ordering Rules

### 3.1 Primary Sort: Composite Score (Descending)

From the scoring engine (`scoreCandidate()`), candidates arrive pre-sorted by:

```
score = -2.2 × editDistance + 1.0 × bigramOverlap + 0.8 × log(frequency)
```

**This order is authoritative.** The UI does not re-sort.

### 3.2 Display Count

| Context | Max Candidates Shown |
|---------|---------------------|
| Default | 5 |
| Short swipe (≤4 keys) | 3 |
| Long swipe (≥10 keys) | 6 |

**Rationale**: Short swipes are often simple words; fewer candidates reduce visual noise. Long swipes benefit from more options due to increased ambiguity.

### 3.3 Tie-Breaking (within scoring)

If scores are equal (rare), secondary sort by:
1. Shorter word first (fewer characters)
2. Higher raw frequency
3. Alphabetical (deterministic fallback)

### 3.4 Rejection Threshold

Candidates with `score < -5.0` are filtered out before display. If all candidates are rejected, show placeholder:

```
"No matches found"
```

---

## 4. Candidate Bar Visual Design

### 4.1 Layout Specification

```
┌────────────────────────────────────────────────────────────────────────────┐
│  ●  hello   │   ○  jello   │   ○  hallo   │   ○  yello   │   ○  bello     │
│  [TOP]          [2]            [3]            [4]            [5]           │
└────────────────────────────────────────────────────────────────────────────┘
     │                                                               │
     │◀─────────────────── Candidate Bar (40px height) ─────────────▶│
```

### 4.2 Visual Properties

| Element | Property | Value |
|---------|----------|-------|
| Bar background | Color | `#0f0f1a` (very dark blue-black) |
| Bar height | Fixed | `40px` |
| Bar radius | Corner | `6px` |
| | | |
| Candidate pill | Min width | `70px` |
| Candidate pill | Max width | `120px` (truncate with ellipsis) |
| Candidate pill | Height | `32px` |
| Candidate pill | Margin | `4px` horizontal |
| Candidate pill | Radius | `4px` |
| | | |
| Top candidate | Background | `#2a4a6a` (muted blue, highlighted) |
| Top candidate | Border | `2px solid #88c0d0` (accent) |
| Top candidate | Indicator | `●` filled circle prefix |
| | | |
| Other candidates | Background | `#2a2a4a` (neutral dark) |
| Other candidates | Border | `1px solid #3a3a5a` |
| Other candidates | Indicator | `○` hollow circle prefix |
| | | |
| Hover state | Background | `#3a4a6a` (brightened) |
| Hover state | Border | `2px solid #a3be8c` (green accent) |
| | | |
| Active/pressed | Background | `#4a4a8a` (purple accent) |
| | | |
| Text | Font size | `14px` |
| Text | Color | `#eceff4` (off-white) |
| Text | Family | System sans-serif |

### 4.3 Animation Properties

| Transition | Duration | Easing |
|------------|----------|--------|
| Candidate appear | 150ms | ease-out |
| Candidate fade | 200ms | ease-in |
| Hover highlight | 100ms | ease-in-out |
| Press feedback | 50ms | linear |

---

## 5. Hover vs Click Selection

### 5.1 Hover Behavior (Preview)

When pointer hovers over a candidate pill:

1. **Visual feedback**: Apply hover state styles (green border)
2. **No action**: Hovering does NOT select or commit
3. **Top indicator update**: Do NOT change the "top" indicator on hover

**Rationale**: Mouse/trackpad users frequently pass over elements accidentally. Hover-to-select would cause many mistakes. We provide visual feedback only.

```cpp
// QML hover handling
onEntered: {
    candidatePill.state = "hovered"
    // NO auto-selection, NO preedit update
}
onExited: {
    candidatePill.state = isTopCandidate ? "top" : "normal"
}
```

### 5.2 Click Behavior (Commit)

When pointer clicks a candidate pill:

1. **Immediate commit**: Send selected word to application
2. **Clear candidates**: Remove candidate bar
3. **No trailing space**: Do not append space automatically
4. **Log acceptance**: Record selection for learning

```cpp
// QML click handling
onClicked: {
    bridge.commitCandidate(modelData)
    root.swipeCandidates = []
}

// Engine side
void handleCandidateCommit(const std::string& word) {
    ic->commitString(word);
    MKLOG(Info) << "CommitCand word=" << word << " space=0";
    candidateMode_ = false;
    currentCandidates_.clear();
    sendToUI("{\"type\":\"swipe_candidates\",\"candidates\":[]}\\n");
}
```

### 5.3 Keyboard Shortcuts (Alternative Selection)

| Key | Action |
|-----|--------|
| `1`-`5` (number keys) | Select candidate by index |
| `Tab` | Cycle top candidate forward |
| `Shift+Tab` | Cycle top candidate backward |
| `Escape` | Cancel candidates, keep nothing |

**Note**: These require physical keyboard input forwarding (v0.3+).

---

## 6. Auto-Commit Rules

### 6.1 Auto-Commit Triggers

When candidates are active, the following key presses auto-commit the **top candidate**:

| Key | Commit Behavior | Post-Commit Action |
|-----|-----------------|-------------------|
| `Space` | Commit top + append space | Clear candidates |
| `Enter` | Commit top (no space) | Clear candidates, send Enter |
| `.` (period) | Commit top + append `.` | Clear candidates |
| `,` (comma) | Commit top + append `,` | Clear candidates |
| `?` | Commit top + append `?` | Clear candidates |
| `!` | Commit top + append `!` | Clear candidates |
| `;` | Commit top + append `;` | Clear candidates |
| `:` | Commit top + append `:` | Clear candidates |

### 6.2 Auto-Commit Implementation

```cpp
void MagicKeyboardEngine::handleKeyPress(const std::string& key) {
    if (candidateMode_ && !currentCandidates_.empty()) {
        // Punctuation auto-commit
        if (isPunctuation(key)) {
            commitTopCandidate();
            // Then commit the punctuation itself
            currentIC_->commitString(key);
            return;
        }
        
        if (key == "space") {
            commitTopCandidateWithSpace();
            return;
        }
        
        if (key == "enter") {
            commitTopCandidate();
            // Forward Enter key to application
            currentIC_->forwardKey(fcitx::Key(FcitxKey_Return), false);
            currentIC_->forwardKey(fcitx::Key(FcitxKey_Return), true);
            return;
        }
        
        // Letter key: implicit commit then continue typing
        if (isLetter(key)) {
            commitTopCandidate();
            currentIC_->commitString(key);
            return;
        }
    }
    
    // ... normal key handling
}

bool isPunctuation(const std::string& key) {
    static const std::set<std::string> punct = {
        ".", ",", "?", "!", ";", ":", "'", "\""
    };
    return punct.count(key) > 0;
}
```

### 6.3 Implicit Commit on New Input

When a **letter key** is pressed while candidates are active:

1. Commit top candidate (no space)
2. Immediately process the new letter

This allows rapid "swipe → tap correction" workflows:

```
User swipes "helo" → candidates: [hello, help, hero]
User taps 'w' → commits "hello", then commits "w"
Result: "hellow" (user can backspace to fix)
```

**Alternative design (rejected)**: Append letter to preedit and re-search. Rejected because it's computationally expensive and creates ambiguity about intent.

---

## 7. Backspace Interaction

### 7.1 Candidate Mode Backspace

When candidates are visible and user presses Backspace:

| State | Backspace Effect |
|-------|------------------|
| Candidates showing | Clear candidates, commit nothing |
| No candidates | Normal backspace (delete previous char) |

### 7.2 Implementation

```cpp
void MagicKeyboardEngine::handleKeyPress(const std::string& key) {
    if (candidateMode_ && key == "backspace") {
        // Clear candidates without committing
        candidateMode_ = false;
        currentCandidates_.clear();
        sendToUI("{\"type\":\"swipe_candidates\",\"candidates\":[]}\\n");
        MKLOG(Info) << "CandidateClear reason=backspace";
        return; // Do NOT forward backspace
    }
    
    // ... rest of handling
}
```

### 7.3 Double-Backspace (Future Enhancement v0.3+)

If user presses Backspace **twice** within 500ms while candidates are showing:

1. First press: Clear candidates (as above)
2. Second press: Normal backspace (delete char)

This provides escape hatch when swipe was completely wrong.

---

## 8. Timing Logic

### 8.1 Candidate Display Timing

```
                    Swipe End
                       │
                       ▼
        ┌──────────────┴──────────────┐
        │        Engine Processing     │
        │   (mapPath → candidates)     │
        │         ~5-20ms              │
        └──────────────┬──────────────┘
                       │
                       ▼
        ┌──────────────┴──────────────┐
        │     Socket Message → UI      │
        │          ~1-2ms              │
        └──────────────┬──────────────┘
                       │
                       ▼
        ┌──────────────┴──────────────┐
        │      QML State Update        │
        │   (render candidate bar)     │
        │          ~5-10ms             │
        └──────────────┬──────────────┘
                       │
                       ▼
                CANDIDATES VISIBLE
                
        Total Budget: <35ms from swipe-end to visible
```

### 8.2 Auto-Clear Timeout

If no user action occurs within **10 seconds** of candidates appearing:

1. Clear candidates
2. Commit nothing
3. Log: `CandidateClear reason=timeout`

**Rationale**: Prevents stale candidates from lingering if user switches apps.

### 8.3 Focus-Loss Clear

If focus leaves the text field while candidates are showing:

1. Clear candidates
2. Commit nothing
3. Log: `CandidateClear reason=focus_lost`

---

## 9. State Machine

### 9.1 States

| State | Description | Candidate Bar Visibility |
|-------|-------------|-------------------------|
| `IDLE` | No active swipe, no candidates | Hidden |
| `SWIPING` | Swipe in progress | Shows "Swiping..." |
| `CANDIDATES` | Candidates displayed, awaiting selection | Visible |
| `COMMITTING` | Selection made, committing | Brief highlight |

### 9.2 State Diagram

```
                    ┌──────────────────────────────────────────┐
                    │                                          │
                    ▼                                          │
              ┌──────────┐                                     │
              │   IDLE   │◀──────────────────────┐             │
              └────┬─────┘                       │             │
                   │                             │             │
            mousedown                     timeout/focus_lost   │
                   │                             │             │
                   ▼                             │             │
              ┌──────────┐                       │             │
              │ SWIPING  │───────┐               │             │
              └────┬─────┘       │               │             │
                   │             │               │             │
        swipe detected     tap release           │             │
        (past deadzone)    (no swipe)            │             │
                   │             │               │             │
                   ▼             ▼               │             │
              ┌──────────┐   send key            │             │
              │CANDIDATES│◀───────────           │             │
              └────┬─────┘                       │             │
                   │                             │             │
    ┌──────────────┼──────────────┬──────────────┤             │
    │              │              │              │             │
 click          space/        backspace       letter           │
 candidate      enter/punct   pressed         pressed          │
    │              │              │              │             │
    ▼              ▼              ▼              ▼             │
┌──────────┐   commit        clear         implicit            │
│COMMITTING│   top+          candidates    commit              │
└────┬─────┘   action                      +letter             │
     │              │              │              │             │
     │              │              │              │             │
     └──────────────┴──────────────┴──────────────┴─────────────┘
                             │
                             ▼
                         ┌──────────┐
                         │   IDLE   │
                         └──────────┘
```

### 9.3 Engine State Variables

```cpp
// In MagicKeyboardEngine
bool candidateMode_ = false;                    // True when candidates active
std::vector<Candidate> currentCandidates_;      // Current ranked list
std::chrono::steady_clock::time_point candidateShowTime_;  // For timeout
```

---

## 10. Learning Acceptance Signals (v0.3+)

### 10.1 Signal Types

| Signal | Description | Data Captured |
|--------|-------------|---------------|
| `CandidateAccepted` | User explicitly clicked/selected | word, rank, keySequence |
| `TopAutoCommitted` | Space/Enter auto-committed top | word, keySequence |
| `CandidateRejected` | Backspace cleared candidates | keySequence, shownCandidates |
| `CandidateIgnored` | Timeout/focus-lost | keySequence, shownCandidates |

### 10.2 Logging Format

```
# Explicit selection
AcceptCand word=hello rank=0 keys=ghelljo shown=5

# Auto-commit (top)
AcceptCand word=hello rank=0 keys=ghelljo shown=5 auto=space

# Rejection (backspace)
RejectCand keys=ghelljo shown=[hello,jello,hallo,yello,help]

# Ignored (timeout)
IgnoreCand keys=werdd shown=[word,weird,weird] reason=timeout
```

### 10.3 Usage (Future)

These signals feed into:
- User-specific frequency boosting
- Misspelling pattern detection
- Swipe path calibration

---

## 11. Edge Cases

### 11.1 Empty Candidates

| Trigger | Behavior |
|---------|----------|
| Swipe produces no valid dictionary matches | Show "No matches" placeholder |
| User continues typing after "No matches" | Clear placeholder, process key normally |
| User backspaces after "No matches" | Clear placeholder, do nothing else |

### 11.2 Single Candidate

If only one candidate exists and scores above threshold:

- Still display candidate bar (user may want to reject)
- Do NOT auto-commit immediately
- Follow normal selection rules

**Rationale**: User may prefer to backspace and re-type rather than accept wrong word.

### 11.3 Very Long Words (Edge)

For words > 15 characters:

- Truncate display with ellipsis: `"antidisestabl..."`
- Full word visible on hover (tooltip)
- Click still commits full word

### 11.4 Rapid Swipes

If user swipes again before selecting from previous candidates:

1. Clear previous candidates (no commit)
2. Process new swipe
3. Show new candidates

---

## 12. QML Component Specification

### 12.1 CandidateBar Component

```qml
// CandidateBar.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: candidateBar
    width: parent.width
    height: 40
    color: "#0f0f1a"
    radius: 6
    
    property var candidates: []
    property bool isActive: candidates.length > 0
    property int hoveredIndex: -1
    
    // Placeholder when swiping
    Text {
        anchors.centerIn: parent
        visible: root.isSwiping || candidates.length === 0
        text: root.isSwiping ? "Swiping..." : "Candidates"
        color: root.isSwiping ? "#00d2ff" : "#555"
        font.pixelSize: 14
    }
    
    // Candidate pills
    RowLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 8
        visible: !root.isSwiping && candidates.length > 0
        
        Repeater {
            model: candidates
            
            Rectangle {
                id: pill
                Layout.fillHeight: true
                Layout.preferredWidth: Math.max(70, pillText.implicitWidth + 24)
                Layout.maximumWidth: 120
                
                property bool isTop: index === 0
                property bool isHovered: candidateBar.hoveredIndex === index
                
                color: isHovered ? "#3a4a6a" : (isTop ? "#2a4a6a" : "#2a2a4a")
                radius: 4
                border.color: isHovered ? "#a3be8c" : (isTop ? "#88c0d0" : "#3a3a5a")
                border.width: (isHovered || isTop) ? 2 : 1
                
                Row {
                    anchors.centerIn: parent
                    spacing: 4
                    
                    Text {
                        text: pill.isTop ? "●" : "○"
                        color: "#88c0d0"
                        font.pixelSize: 10
                    }
                    
                    Text {
                        id: pillText
                        text: modelData.length > 12 
                              ? modelData.substring(0, 11) + "…" 
                              : modelData
                        color: "#eceff4"
                        font.pixelSize: 14
                        elide: Text.ElideNone
                    }
                }
                
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    
                    onEntered: candidateBar.hoveredIndex = index
                    onExited: if (candidateBar.hoveredIndex === index) 
                                  candidateBar.hoveredIndex = -1
                    onClicked: {
                        bridge.commitCandidate(modelData)
                        root.swipeCandidates = []
                    }
                }
                
                Behavior on color { ColorAnimation { duration: 100 } }
                Behavior on border.color { ColorAnimation { duration: 100 } }
            }
        }
        
        // Spacer to push pills left
        Item { Layout.fillWidth: true }
    }
}
```

### 12.2 Integration Points

```qml
// In KeyboardWindow.qml

// Connection to receive candidates from engine
Connections {
    target: bridge
    function onSwipeCandidatesReceived(candidates) {
        root.swipeCandidates = candidates
    }
}

// Candidate bar in layout
ColumnLayout {
    id: mainLayout
    anchors.fill: parent
    
    CandidateBar {
        Layout.fillWidth: true
        candidates: root.swipeCandidates
    }
    
    // ... keyboard rows
}
```

---

## 13. Testing Matrix

### 13.1 Selection Tests

| Test | Steps | Expected |
|------|-------|----------|
| Click top candidate | Swipe "hello", click first pill | "hello" commits, candidates clear |
| Click lower candidate | Swipe "hello", click "hallo" (3rd) | "hallo" commits, candidates clear |
| Hover no commit | Hover over candidates | Visual feedback only, no commit |
| Space auto-commit | Swipe "hello", press Space | "hello " commits (with space) |
| Enter auto-commit | Swipe "hello", press Enter | "hello" commits, newline inserted |
| Period auto-commit | Swipe "hello", press `.` | "hello." commits |

### 13.2 Backspace Tests

| Test | Steps | Expected |
|------|-------|----------|
| Backspace clears | Swipe, candidates show, press Backspace | Candidates clear, nothing committed |
| Backspace then type | Clear candidates, press 'a' | Candidates gone, 'a' committed |
| Double backspace | Clear, quickly press Backspace again | Second press deletes char |

### 13.3 Edge Case Tests

| Test | Steps | Expected |
|------|-------|----------|
| No matches | Swipe random path off-keys | "No matches" placeholder |
| Rapid re-swipe | Swipe "hello", immediately swipe "world" | First cleared, second shows |
| Focus loss | Swipe, switch app | Candidates clear silently |
| Timeout | Swipe, wait 10+ seconds | Candidates clear silently |

### 13.4 Visual Tests

| Test | Expected |
|------|----------|
| Top candidate style | Blue background, solid circle, prominent border |
| Hover style | Green border, brightened background |
| Press style | Purple background, instant response |
| Long word truncation | Shows "antidisestab…" with full word on commit |

---

## 14. Key Outputs Summary

### 14.1 Candidate UX Rules

1. **Ordering**: Score-based (edit distance + bigrams + frequency), never re-sort in UI
2. **Display count**: 3-6 candidates based on swipe length
3. **Hover**: Visual feedback only, no selection
4. **Click**: Immediate commit, no trailing space
5. **Rejection threshold**: `score < -5.0` filtered out

### 14.2 Commit Timing Logic

| Trigger | Commit | Suffix | Clear Candidates |
|---------|--------|--------|-----------------|
| Click candidate | Selected word | None | Yes |
| Space key | Top candidate | Space | Yes |
| Enter key | Top candidate | Newline | Yes |
| Punctuation | Top candidate | Punctuation | Yes |
| Letter key | Top candidate | None | Yes, then process letter |
| Backspace | None | N/A | Yes |
| Timeout | None | N/A | Yes |
| Focus loss | None | N/A | Yes |
| New swipe | None | N/A | Yes, then show new |

### 14.3 Auto-Clear Timeout

- Duration: 10 seconds
- Effect: Clear candidates, commit nothing
- Logging: `CandidateClear reason=timeout`

---

## 15. Implementation Checklist

### v0.2 (Current)

- [x] Basic candidate bar in QML
- [x] Click-to-commit via `commitCandidate()` 
- [x] Backspace clears candidates
- [x] Space commits top + space
- [x] Enter commits top + newline
- [ ] Proper hover styling (green border)
- [ ] Top candidate visual distinction
- [ ] Punctuation auto-commit
- [ ] Implicit letter commit

### v0.3 (Future)

- [ ] Candidate timeout (10s)
- [ ] Focus-loss clear
- [ ] Learning signal logging
- [ ] Keyboard shortcuts (1-5, Tab, Escape)
- [ ] Double-backspace for delete
- [ ] Tooltip for truncated words

---

## 16. Revision History

| Date | Change |
|------|--------|
| 2024-12-29 | Initial Candidate Bar & Selection design document |
