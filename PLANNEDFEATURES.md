Magic Keyboard — Planned Features & Roadmap

Status: Planning / Not Yet Implemented
Phase: Daily Usability (Stability-First)
Scope: Engine (fcitx5), UI (Qt/QML), UX polish
Non-Goals: No protocol breaks, no blocking paths, no speculative ML

⸻

1. Swipe Typing Intelligence (Planned)

1.1 Swipe Decoder (Path → Word Candidates)

Goal:
Convert a noisy continuous swipe path into ranked word candidates, instead of literal “all keys touched”.

Planned Capabilities
	•	Continuous path resampling
	•	Distance-based key probability modeling
	•	Prefix-pruned dictionary traversal (trie)
	•	Beam search to produce top-N word candidates
	•	Deterministic, low-latency execution

Constraints
	•	Engine-side only (fcitx5 addon)
	•	No blocking operations
	•	Decode on swipe end (or rate-limited), never per frame
	•	No auto-commit; commit remains explicit

⸻

1.2 Adaptive Learning (Personalization)

Goal:
Keyboard becomes more accurate over time based on actual committed words.

Planned Learning Signals
	•	Unigram frequency
Boost words the user commits often.
	•	Bigram context (optional)
Boost words that frequently follow the previously committed word.

Design Principles
	•	Learn only on explicit commit
	•	Bounded memory usage
	•	Persisted to small local state file
	•	Safe fallback if data is missing or corrupt
	•	No neural networks, no background training

⸻

2. Swipe Engine Tuning & Sensitivity

2.1 Swipe Sensitivity Adjustment

Goal:
Allow users to tune how “forgiving” swipe recognition feels.

Planned Controls
	•	Minimum movement threshold (tap vs swipe)
	•	Jitter filtering strength
	•	Path smoothing factor
	•	Key attraction radius (distance falloff)

Implementation Notes
	•	UI-controlled settings
	•	Engine consumes normalized parameters
	•	Defaults tuned for Steam Deck touchpads
	•	Changes apply immediately, no restart required

⸻

3. Window & Layout Behavior

3.1 Resizable Window

Goal:
Allow the keyboard window to be resized without breaking layout or input mapping.

Planned Behavior
	•	Drag handles or corner resize
	•	Layout scales proportionally
	•	Key hitboxes update dynamically
	•	No reflow stalls during resize

Constraints
	•	UI must remain escapable
	•	No exclusive grabs
	•	Hidden state ignores resize input

⸻

3.2 Transparency Adjustment

Goal:
User-adjustable opacity to reduce visual obstruction.

Planned Behavior
	•	Adjustable opacity slider
	•	Independent of theme
	•	Applies only to UI (engine unaffected)
	•	Safe minimum opacity to maintain usability

⸻

3.3 Snap to Text Input Location

Goal:
Reduce eye travel by positioning keyboard near the active text field.

Planned Modes
	•	Disabled (fixed position)
	•	Snap below caret
	•	Snap above caret (fallback)
	•	Smart clamp to screen edges

Constraints
	•	Must not obscure the caret
	•	Must never trap focus
	•	Must gracefully fall back if caret position unavailable

⸻

4. Themes & Visual Customization

4.1 Theme System

Goal:
Support multiple visual styles without touching engine logic.

Planned Theme Attributes
	•	Key colors
	•	Background style
	•	Key borders / rounding
	•	Font size and weight
	•	Highlight / swipe path color

Design Rules
	•	Themes are UI-only
	•	Hot-swappable at runtime
	•	No logic in QML themes
	•	Defaults remain conservative and readable

⸻

4.2 Steam Deck Ergonomics

Goal:
Thumb-friendly layouts optimized for handheld use.

Planned Options
	•	Compact layout
	•	Split layout
	•	Thumb-reach optimized spacing
	•	Larger swipe target zones (visual only)

⸻

5. UX & Reliability Guarantees (Non-Negotiable)

These are design invariants, not optional features:
	•	Escape key always hides UI
	•	Hidden UI processes no input
	•	Engine survives UI crash/restart
	•	UI survives engine restart
	•	IPC remains backward compatible
	•	No input deadlocks
	•	No auto-commits without explicit user action

⸻

6. Out of Scope (For Now)

The following are explicitly not planned in the daily usability phase:
	•	Cloud sync of learned data
	•	Neural language models
	•	Background training threads
	•	Protocol-breaking IPC changes
	•	Per-app custom behavior
	•	Gesture macros beyond typing

⸻

7. Planned Implementation Order (High-Level)
	1.	Swipe decoder (basic candidate ranking)
	2.	Adaptive unigram learning
	3.	Sensitivity tuning controls
	4.	Transparency + resize support
	5.	Theme system
	6.	Snap-to-caret positioning
	7.	Optional bigram learning
	8.	UI candidate bar polish

Each step must:
	•	Compile cleanly
	•	Preserve existing behavior
	•	Be reversible
	•	Introduce no regressions

⸻

8. Guiding Principle

“Phone-smooth feel, desktop-grade reliability.”

Magic Keyboard favors:
	•	Determinism over magic
	•	Stability over novelty
	•	Explicit user control over automation
