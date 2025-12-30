#pragma once

/**
 * Magic Keyboard - Settings System
 *
 * Provides persistent user preferences using XDG standard paths.
 * All settings are immediately applied without restart required.
 */

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

namespace magickeyboard {

// ============================================================================
// Settings Structure
// ============================================================================

struct Settings {
  // === Swipe Sensitivity (Section 2.1) ===
  // Minimum movement threshold (tap vs swipe) in pixels
  double swipeThresholdPx = 12.0;
  // Jitter filtering strength (0.0 = none, 1.0 = max smoothing)
  double jitterFilter = 0.35;
  // Path smoothing factor (EMA alpha: higher = more responsive, lower = smoother)
  double pathSmoothing = 0.35;
  // Key attraction radius falloff (pixels)
  double keyAttractionRadius = 60.0;

  // === Window & Layout (Section 3) ===
  // Window opacity (0.3 = very transparent, 1.0 = fully opaque)
  double windowOpacity = 1.0;
  // Window scale factor (0.5 = half size, 2.0 = double size)
  double windowScale = 1.0;
  // Snap to caret mode: 0=disabled, 1=below, 2=above, 3=smart
  int snapToCaretMode = 0;

  // === Theme (Section 4) ===
  // Active theme name (empty = default)
  std::string activeTheme = "";

  // === Layout ===
  // Active keyboard layout
  std::string activeLayout = "qwerty";

  // Equality operator for change detection
  bool operator==(const Settings &other) const {
    return swipeThresholdPx == other.swipeThresholdPx &&
           jitterFilter == other.jitterFilter &&
           pathSmoothing == other.pathSmoothing &&
           keyAttractionRadius == other.keyAttractionRadius &&
           windowOpacity == other.windowOpacity &&
           windowScale == other.windowScale &&
           snapToCaretMode == other.snapToCaretMode &&
           activeTheme == other.activeTheme &&
           activeLayout == other.activeLayout;
  }

  bool operator!=(const Settings &other) const { return !(*this == other); }
};

// ============================================================================
// Settings Manager
// ============================================================================

class SettingsManager {
public:
  // Get singleton instance
  static SettingsManager &instance();

  // Load settings from disk (called on engine startup)
  bool load();

  // Save settings to disk
  bool save();

  // Get current settings (thread-safe read)
  Settings get() const;

  // Update settings and notify listeners
  void set(const Settings &newSettings);

  // Update single setting by name (for IPC handling)
  bool setSingle(const std::string &key, const std::string &value);

  // Register a callback for settings changes
  using ChangeCallback = std::function<void(const Settings &)>;
  void onChanged(ChangeCallback callback);

  // Get settings file path
  std::string getSettingsPath() const;

  // Get user data directory path
  std::string getUserDataDir() const;

private:
  SettingsManager() = default;
  SettingsManager(const SettingsManager &) = delete;
  SettingsManager &operator=(const SettingsManager &) = delete;

  // Ensure user data directory exists
  bool ensureDataDir() const;

  mutable std::mutex mutex_;
  Settings settings_;
  std::vector<ChangeCallback> callbacks_;
  std::atomic<bool> loaded_{false};
};

} // namespace magickeyboard
