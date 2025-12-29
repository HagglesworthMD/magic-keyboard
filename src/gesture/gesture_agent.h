/**
 * Magic Keyboard - Gesture Agent
 *
 * Swipe typing gesture detection via pointer click-drag input.
 * This agent owns all pointer gesture logic, separate from touch typing.
 *
 * Reference: docs/GESTURE_AGENT.md
 */
#pragma once

#include <chrono>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace magickeyboard {
namespace gesture {

// ============================================================================
// Configuration Constants
// ============================================================================

struct GestureConfig {
  // Deadzone: pixels movement required before swipe is recognized
  double deadzoneRadius = 10.0;

  // Time threshold: milliseconds before swipe can be recognized
  double timeThresholdMs = 35.0;

  // EMA smoothing factor (0.0 = no smoothing, 1.0 = raw input)
  double smoothingAlpha = 0.40;

  // Resample distance: uniform spacing between path points
  double resampleDistance = 7.0;

  // Stationary timeout: ms of no movement to auto-complete (0 = disabled)
  double stationaryTimeoutMs = 0.0;

  // Hysteresis ratio: new key must be this much closer to switch
  double hysteresisRatio = 0.72;

  // Minimum distance gap for key switch (absolute pixels)
  double minDistanceGap = 6.0;

  // Minimum consecutive samples to confirm key switch
  int minConsecutiveSamples = 2;

  // Minimum dwell count to not be considered a bounce
  int minDwellForBounce = 2;
};

// ============================================================================
// Geometry Types
// ============================================================================

struct Point {
  double x = 0;
  double y = 0;

  Point() = default;
  Point(double x_, double y_) : x(x_), y(y_) {}

  double distanceSquaredTo(const Point &other) const {
    double dx = x - other.x;
    double dy = y - other.y;
    return dx * dx + dy * dy;
  }

  double distanceTo(const Point &other) const {
    return std::sqrt(distanceSquaredTo(other));
  }
};

struct Rect {
  double x = 0;
  double y = 0;
  double w = 0;
  double h = 0;

  bool contains(const Point &p) const {
    return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
  }
};

struct PathPoint {
  Point window; // Window-space coordinates (for rendering)
  Point layout; // Layout-space coordinates (for key mapping)
  uint64_t timestamp = 0;
};

// ============================================================================
// Key Definition
// ============================================================================

struct Key {
  std::string id; // Key identifier (e.g., "a", "backspace")
  Rect rect;      // Bounding rectangle
  Point center;   // Center point for distance calculations

  Key() = default;
  Key(const std::string &id_, const Rect &r)
      : id(id_), rect(r), center(r.x + r.w / 2, r.y + r.h / 2) {}
};

// ============================================================================
// Gesture State Machine
// ============================================================================

enum class GestureState {
  Idle,       // Waiting for input
  TapPending, // Button down, awaiting classification
  Swiping,    // Active swipe gesture
  Completed,  // Swipe complete (transient)
  Tapped      // Tap detected (transient)
};

inline const char *stateToString(GestureState state) {
  switch (state) {
  case GestureState::Idle:
    return "Idle";
  case GestureState::TapPending:
    return "TapPending";
  case GestureState::Swiping:
    return "Swiping";
  case GestureState::Completed:
    return "Completed";
  case GestureState::Tapped:
    return "Tapped";
  }
  return "Unknown";
}

// ============================================================================
// Gesture Result Types
// ============================================================================

struct TapResult {
  std::string keyId;
  Point position;
};

struct SwipeResult {
  std::vector<PathPoint> path;
  std::vector<std::string> keySequence;
  double durationMs;
};

// ============================================================================
// Gesture Agent
// ============================================================================

class GestureAgent {
public:
  using TapCallback = std::function<void(const TapResult &)>;
  using SwipeCallback = std::function<void(const SwipeResult &)>;

  explicit GestureAgent(const GestureConfig &config = GestureConfig());
  ~GestureAgent() = default;

  // Configuration
  void setConfig(const GestureConfig &config);
  const GestureConfig &config() const { return config_; }

  // Key layout (for hit-testing)
  void setKeys(const std::vector<Key> &keys);
  const std::vector<Key> &keys() const { return keys_; }

  // Callbacks
  void setTapCallback(TapCallback callback) { onTap_ = std::move(callback); }
  void setSwipeCallback(SwipeCallback callback) {
    onSwipe_ = std::move(callback);
  }

  // Input events (call these from UI layer)
  void pointerDown(const Point &windowPos, const Point &layoutPos,
                   uint64_t timestamp);
  void pointerMove(const Point &windowPos, const Point &layoutPos,
                   uint64_t timestamp);
  void pointerUp(const Point &windowPos, const Point &layoutPos,
                 uint64_t timestamp);

  // State queries
  GestureState state() const { return state_; }
  bool isSwiping() const { return state_ == GestureState::Swiping; }
  const std::vector<PathPoint> &currentPath() const { return path_; }

  // Reset to idle state
  void reset();

private:
  // State transitions
  void transitionTo(GestureState newState);

  // Smoothing
  Point smooth(const Point &raw, const Point &prev) const;

  // Path processing
  bool shouldAddSample(const PathPoint &candidate) const;
  void resamplePath();

  // Key mapping
  const Key *findNearestKey(const Point &p) const;
  bool shouldSwitchKey(const Key *current, const Key *candidate,
                       const Point &p) const;
  std::vector<std::string> mapPathToKeys() const;

  // Post-processing
  std::vector<std::string>
  removeBouncesAndDuplicates(const std::vector<std::string> &raw) const;

  // Configuration
  GestureConfig config_;

  // Key layout
  std::vector<Key> keys_;

  // Current state
  GestureState state_ = GestureState::Idle;

  // Gesture start info
  Point startPosWindow_;
  Point startPosLayout_;
  uint64_t startTime_ = 0;

  // Smoothed position tracking
  Point lastSmoothedWindow_;
  Point lastSmoothedLayout_;

  // Path accumulator
  std::vector<PathPoint> path_;

  // Callbacks
  TapCallback onTap_;
  SwipeCallback onSwipe_;

  // Hysteresis state
  const Key *currentKey_ = nullptr;
  const Key *candidateKey_ = nullptr;
  int candidateCount_ = 0;
};

// ============================================================================
// Implementation
// ============================================================================

inline GestureAgent::GestureAgent(const GestureConfig &config)
    : config_(config) {}

inline void GestureAgent::setConfig(const GestureConfig &config) {
  config_ = config;
}

inline void GestureAgent::setKeys(const std::vector<Key> &keys) {
  keys_ = keys;
}

inline void GestureAgent::pointerDown(const Point &windowPos,
                                      const Point &layoutPos,
                                      uint64_t timestamp) {
  reset();

  startPosWindow_ = windowPos;
  startPosLayout_ = layoutPos;
  startTime_ = timestamp;
  lastSmoothedWindow_ = windowPos;
  lastSmoothedLayout_ = layoutPos;

  transitionTo(GestureState::TapPending);
}

inline void GestureAgent::pointerMove(const Point &windowPos,
                                      const Point &layoutPos,
                                      uint64_t timestamp) {
  if (state_ == GestureState::Idle) {
    return; // Ignore moves without button down
  }

  // Smooth the input
  Point smoothedWindow = smooth(windowPos, lastSmoothedWindow_);
  Point smoothedLayout = smooth(layoutPos, lastSmoothedLayout_);

  if (state_ == GestureState::TapPending) {
    // Check if we've broken the deadzone
    double dist = startPosWindow_.distanceTo(windowPos);
    double dt = static_cast<double>(timestamp - startTime_);

    if (dist > config_.deadzoneRadius && dt > config_.timeThresholdMs) {
      transitionTo(GestureState::Swiping);

      // Add start of path
      PathPoint start;
      start.window = startPosWindow_;
      start.layout = startPosLayout_;
      start.timestamp = startTime_;
      path_.push_back(start);
    }
  }

  if (state_ == GestureState::Swiping) {
    PathPoint candidate;
    candidate.window = smoothedWindow;
    candidate.layout = smoothedLayout;
    candidate.timestamp = timestamp;

    if (shouldAddSample(candidate)) {
      path_.push_back(candidate);
    }

    lastSmoothedWindow_ = smoothedWindow;
    lastSmoothedLayout_ = smoothedLayout;
  }
}

inline void GestureAgent::pointerUp(const Point &windowPos,
                                    const Point &layoutPos,
                                    uint64_t timestamp) {
  if (state_ == GestureState::TapPending) {
    // Still in deadzone → this is a tap
    transitionTo(GestureState::Tapped);

    if (onTap_) {
      const Key *key = findNearestKey(startPosLayout_);
      TapResult result;
      result.keyId = key ? key->id : "";
      result.position = startPosLayout_;
      onTap_(result);
    }

    transitionTo(GestureState::Idle);
  } else if (state_ == GestureState::Swiping) {
    // Add final point
    Point smoothedWindow = smooth(windowPos, lastSmoothedWindow_);
    Point smoothedLayout = smooth(layoutPos, lastSmoothedLayout_);

    PathPoint finalPoint;
    finalPoint.window = smoothedWindow;
    finalPoint.layout = smoothedLayout;
    finalPoint.timestamp = timestamp;
    path_.push_back(finalPoint);

    transitionTo(GestureState::Completed);

    if (onSwipe_) {
      SwipeResult result;
      result.path = path_;
      result.keySequence = mapPathToKeys();
      result.durationMs = static_cast<double>(timestamp - startTime_);
      onSwipe_(result);
    }

    transitionTo(GestureState::Idle);
  } else {
    transitionTo(GestureState::Idle);
  }
}

inline void GestureAgent::reset() {
  state_ = GestureState::Idle;
  path_.clear();
  currentKey_ = nullptr;
  candidateKey_ = nullptr;
  candidateCount_ = 0;
}

inline void GestureAgent::transitionTo(GestureState newState) {
  // Could add logging here
  state_ = newState;
}

inline Point GestureAgent::smooth(const Point &raw, const Point &prev) const {
  double a = config_.smoothingAlpha;
  return Point(a * raw.x + (1 - a) * prev.x, a * raw.y + (1 - a) * prev.y);
}

inline bool GestureAgent::shouldAddSample(const PathPoint &candidate) const {
  if (path_.empty()) {
    return true;
  }

  const auto &last = path_.back();
  double dist = candidate.window.distanceTo(last.window);
  return dist >= config_.resampleDistance;
}

inline const Key *GestureAgent::findNearestKey(const Point &p) const {
  const Key *best = nullptr;
  double bestDistSq = 1e18;

  // Priority 1: Inside rect
  for (const auto &k : keys_) {
    if (k.rect.contains(p)) {
      return &k;
    }
  }

  // Priority 2: Nearest center
  for (const auto &k : keys_) {
    double d2 = k.center.distanceSquaredTo(p);
    if (d2 < bestDistSq) {
      bestDistSq = d2;
      best = &k;
    }
  }

  return best;
}

inline bool GestureAgent::shouldSwitchKey(const Key *current,
                                          const Key *candidate,
                                          const Point &p) const {
  if (!current || !candidate) {
    return true;
  }

  // Inside rect always wins
  if (candidate->rect.contains(p)) {
    return true;
  }

  double dCur = current->center.distanceTo(p);
  double dCand = candidate->center.distanceTo(p);

  // Must be significantly closer
  if (dCand < dCur * config_.hysteresisRatio &&
      (dCur - dCand) > config_.minDistanceGap) {
    return true;
  }

  return false;
}

inline std::vector<std::string> GestureAgent::mapPathToKeys() const {
  if (path_.empty() || keys_.empty()) {
    return {};
  }

  std::vector<std::string> rawSequence;
  const Key *currentKey = nullptr;
  const Key *candidateKey = nullptr;
  int candidateCount = 0;

  for (const auto &pt : path_) {
    const Key *bestKey = findNearestKey(pt.layout);
    if (!bestKey) {
      continue;
    }

    if (currentKey == nullptr) {
      currentKey = bestKey;
      rawSequence.push_back(currentKey->id);
    } else if (bestKey != currentKey) {
      bool accept = shouldSwitchKey(currentKey, bestKey, pt.layout);

      if (!accept) {
        // Track consecutive samples for candidate
        if (bestKey == candidateKey) {
          candidateCount++;
        } else {
          candidateKey = bestKey;
          candidateCount = 1;
        }

        if (candidateCount >= config_.minConsecutiveSamples) {
          accept = true;
        }
      }

      if (accept) {
        currentKey = bestKey;
        rawSequence.push_back(currentKey->id);
        candidateKey = nullptr;
        candidateCount = 0;
      }
    }
  }

  return removeBouncesAndDuplicates(rawSequence);
}

inline std::vector<std::string> GestureAgent::removeBouncesAndDuplicates(
    const std::vector<std::string> &raw) const {
  if (raw.empty()) {
    return {};
  }

  // Build dwell counts
  std::vector<std::pair<std::string, int>> dwells;
  for (const auto &s : raw) {
    if (dwells.empty() || s != dwells.back().first) {
      dwells.push_back({s, 1});
    } else {
      dwells.back().second++;
    }
  }

  // Remove A-B-A bounces where B has low dwell
  std::vector<std::string> filtered;
  for (size_t i = 0; i < dwells.size(); ++i) {
    if (i > 0 && i < dwells.size() - 1) {
      if (dwells[i - 1].first == dwells[i + 1].first &&
          dwells[i].second < config_.minDwellForBounce) {
        continue; // Skip the "B"
      }
    }
    filtered.push_back(dwells[i].first);
  }

  // Final duplicate collapse
  std::vector<std::string> result;
  for (const auto &s : filtered) {
    if (result.empty() || s != result.back()) {
      result.push_back(s);
    }
  }

  return result;
}

} // namespace gesture
} // namespace magickeyboard
