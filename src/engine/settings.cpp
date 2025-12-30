/**
 * Magic Keyboard - Settings Implementation
 *
 * Uses simple key=value format for persistence.
 * File location: $XDG_DATA_HOME/magic-keyboard/settings.conf
 */

#include "settings.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace magickeyboard {

// ============================================================================
// Singleton Access
// ============================================================================

SettingsManager &SettingsManager::instance() {
  static SettingsManager instance;
  return instance;
}

// ============================================================================
// Path Resolution
// ============================================================================

std::string SettingsManager::getUserDataDir() const {
  const char *xdgDataHome = std::getenv("XDG_DATA_HOME");
  if (xdgDataHome && *xdgDataHome) {
    return std::string(xdgDataHome) + "/magic-keyboard";
  }
  const char *home = std::getenv("HOME");
  if (home && *home) {
    return std::string(home) + "/.local/share/magic-keyboard";
  }
  return "/tmp/magic-keyboard";
}

std::string SettingsManager::getSettingsPath() const {
  return getUserDataDir() + "/settings.conf";
}

bool SettingsManager::ensureDataDir() const {
  std::string dir = getUserDataDir();
  struct stat st;

  // Check if directory exists
  if (stat(dir.c_str(), &st) == 0) {
    return S_ISDIR(st.st_mode);
  }

  // Create parent directory first if needed (for ~/.local/share case)
  std::string parent = dir.substr(0, dir.find_last_of('/'));
  if (!parent.empty() && stat(parent.c_str(), &st) != 0) {
    mkdir(parent.c_str(), 0755);
  }

  // Create the magic-keyboard directory
  return mkdir(dir.c_str(), 0755) == 0;
}

// ============================================================================
// Load/Save Operations
// ============================================================================

bool SettingsManager::load() {
  std::lock_guard<std::mutex> lock(mutex_);

  std::ifstream file(getSettingsPath());
  if (!file.is_open()) {
    // No settings file yet - use defaults
    loaded_ = true;
    return true;
  }

  Settings newSettings;
  std::string line;

  while (std::getline(file, line)) {
    // Skip empty lines and comments
    if (line.empty() || line[0] == '#')
      continue;

    auto eq = line.find('=');
    if (eq == std::string::npos)
      continue;

    std::string key = line.substr(0, eq);
    std::string value = line.substr(eq + 1);

    // Trim whitespace
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
      key.pop_back();
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
      value.erase(0, 1);

    // Parse known keys
    try {
      if (key == "swipe_threshold_px") {
        newSettings.swipeThresholdPx = std::stod(value);
      } else if (key == "jitter_filter") {
        newSettings.jitterFilter = std::stod(value);
      } else if (key == "path_smoothing") {
        newSettings.pathSmoothing = std::stod(value);
      } else if (key == "key_attraction_radius") {
        newSettings.keyAttractionRadius = std::stod(value);
      } else if (key == "window_opacity") {
        newSettings.windowOpacity = std::stod(value);
      } else if (key == "window_scale") {
        newSettings.windowScale = std::stod(value);
      } else if (key == "snap_to_caret_mode") {
        newSettings.snapToCaretMode = std::stoi(value);
      } else if (key == "active_theme") {
        newSettings.activeTheme = value;
      } else if (key == "active_layout") {
        newSettings.activeLayout = value;
      }
    } catch (...) {
      // Invalid value - skip this setting
    }
  }

  settings_ = newSettings;
  loaded_ = true;
  return true;
}

bool SettingsManager::save() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!ensureDataDir()) {
    return false;
  }

  std::ofstream file(getSettingsPath());
  if (!file.is_open()) {
    return false;
  }

  file << "# Magic Keyboard Settings\n";
  file << "# This file is auto-generated. Manual edits are preserved.\n\n";

  file << "# Swipe Sensitivity\n";
  file << "swipe_threshold_px=" << settings_.swipeThresholdPx << "\n";
  file << "jitter_filter=" << settings_.jitterFilter << "\n";
  file << "path_smoothing=" << settings_.pathSmoothing << "\n";
  file << "key_attraction_radius=" << settings_.keyAttractionRadius << "\n\n";

  file << "# Window & Layout\n";
  file << "window_opacity=" << settings_.windowOpacity << "\n";
  file << "window_scale=" << settings_.windowScale << "\n";
  file << "snap_to_caret_mode=" << settings_.snapToCaretMode << "\n\n";

  file << "# Theme\n";
  file << "active_theme=" << settings_.activeTheme << "\n\n";

  file << "# Layout\n";
  file << "active_layout=" << settings_.activeLayout << "\n";

  return file.good();
}

// ============================================================================
// Get/Set Operations
// ============================================================================

Settings SettingsManager::get() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return settings_;
}

void SettingsManager::set(const Settings &newSettings) {
  bool changed = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (settings_ != newSettings) {
      settings_ = newSettings;
      changed = true;
    }
  }

  if (changed) {
    save();
    // Notify callbacks (outside lock to prevent deadlock)
    for (auto &cb : callbacks_) {
      cb(newSettings);
    }
  }
}

bool SettingsManager::setSingle(const std::string &key,
                                const std::string &value) {
  Settings current = get();
  bool recognized = true;

  try {
    if (key == "swipe_threshold_px") {
      current.swipeThresholdPx = std::clamp(std::stod(value), 5.0, 50.0);
    } else if (key == "jitter_filter") {
      current.jitterFilter = std::clamp(std::stod(value), 0.0, 1.0);
    } else if (key == "path_smoothing") {
      current.pathSmoothing = std::clamp(std::stod(value), 0.0, 1.0);
    } else if (key == "key_attraction_radius") {
      current.keyAttractionRadius = std::clamp(std::stod(value), 20.0, 150.0);
    } else if (key == "window_opacity") {
      current.windowOpacity = std::clamp(std::stod(value), 0.3, 1.0);
    } else if (key == "window_scale") {
      current.windowScale = std::clamp(std::stod(value), 0.5, 2.0);
    } else if (key == "snap_to_caret_mode") {
      current.snapToCaretMode = std::clamp(std::stoi(value), 0, 3);
    } else if (key == "active_theme") {
      current.activeTheme = value;
    } else if (key == "active_layout") {
      current.activeLayout = value;
    } else {
      recognized = false;
    }
  } catch (...) {
    return false;
  }

  if (recognized) {
    set(current);
  }
  return recognized;
}

void SettingsManager::onChanged(ChangeCallback callback) {
  callbacks_.push_back(std::move(callback));
}

} // namespace magickeyboard
