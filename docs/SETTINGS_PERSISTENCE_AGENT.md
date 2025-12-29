# Settings & Persistence Agent

**Design Document for Magic Keyboard Configuration and Learning System**

Version 1.0 — Configuration Architecture for Fcitx5-Based On-Screen Keyboard

---

## Executive Summary

This document defines the configuration and persistence system for Magic Keyboard, prioritizing **simplicity**, **safety**, and **future extensibility**. The design follows the principle of "boring reliability" — no exotic storage mechanisms, clear upgrade paths, and fail-safe defaults.

### Design Principles

1. **Plain Text > Binary** — Human-readable config files for debuggability
2. **Fail Open** — Missing config = sensible defaults, no crashes
3. **XDG Compliance** — Standard Linux paths, respects user environment
4. **Atomic Writes** — No corruption on power loss (Steam Deck reality)
5. **Layered Config** — System defaults < User config < Runtime overrides
6. **Future-Proof Schema** — Versioned format, forward-compatible

---

## 1. Storage Location Strategy

### Path Hierarchy (XDG Base Directory Specification)

```
$XDG_CONFIG_HOME/magic-keyboard/          # User config (editable)
├── config.json                           # Main settings
├── learning.json                         # Learned words/frequencies
└── layouts/                              # Custom layout overrides
    └── qwerty-custom.json

$XDG_DATA_HOME/magic-keyboard/            # User data (not typically edited)
├── user-dictionary.txt                   # User-added words
└── word-frequencies.tsv                  # Learned frequency adjustments

$XDG_STATE_HOME/magic-keyboard/           # Ephemeral state
├── session.json                          # Current session state (volatile)
└── crash-recovery.json                   # Last-known-good before crash

System fallbacks (read-only):
/usr/local/share/magic-keyboard/          # SteamOS-safe install location
/usr/share/magic-keyboard/                # Standard system location
```

### Concrete Default Paths

| Variable | Default | Typical Value |
|----------|---------|---------------|
| `$XDG_CONFIG_HOME` | `~/.config` | `/home/deck/.config` |
| `$XDG_DATA_HOME` | `~/.local/share` | `/home/deck/.local/share` |
| `$XDG_STATE_HOME` | `~/.local/state` | `/home/deck/.local/state` |

### Why This Structure

| Concern | Solution |
|---------|----------|
| SteamOS read-only rootfs | User config in `~/.config`, system defaults in `/usr/local/share` |
| Backup-friendly | `~/.config/magic-keyboard/` = complete user preferences |
| Reset mechanism | Delete user config directory → returns to system defaults |
| Multi-user support | Each user has isolated config under their `$HOME` |
| Sync potential | `config.json` is safe to sync; `learning.json` per-device |

---

## 2. Configuration Schema

### Primary Config: `config.json`

```json
{
  "$schema": "https://magic-keyboard.dev/schemas/config-v1.json",
  "version": 1,
  
  "layout": {
    "name": "qwerty",
    "custom": null
  },
  
  "swipe": {
    "enabled": true,
    "sensitivity": 1.0,
    "deadzone_px": 12,
    "sample_rate_hz": 120,
    "smoothing_alpha": 0.35,
    "min_path_samples": 3
  },
  
  "input": {
    "terminal_mode": false,
    "auto_capitalize": true,
    "auto_space_after_punctuation": true,
    "double_space_period": true
  },
  
  "appearance": {
    "theme": "dark",
    "key_sound": false,
    "haptic_feedback": false,
    "candidate_count": 5,
    "show_swipe_trail": true,
    "scale_factor": "auto"
  },
  
  "advanced": {
    "dictionary_path": null,
    "learning_enabled": true,
    "debug_mode": false
  }
}
```

### Schema Versioning Strategy

| Version | Breaking Changes | Migration |
|---------|------------------|-----------|
| v1 | Initial release | N/A |
| v2 (future) | Any key rename/removal | Auto-migrate from v1 |

**Migration Rule**: New versions MUST be able to read all previous versions. Missing fields use defaults.

### Default Value Table

| Setting | Default | Range/Options | Notes |
|---------|---------|---------------|-------|
| `layout.name` | `"qwerty"` | `qwerty`, `dvorak`, `colemak` | Future layouts |
| `swipe.enabled` | `true` | `true/false` | Disable for tap-only mode |
| `swipe.sensitivity` | `1.0` | `0.5 - 2.0` | Multiplier for movement detection |
| `swipe.deadzone_px` | `12` | `6 - 24` | Pixels before swipe activates |
| `swipe.smoothing_alpha` | `0.35` | `0.1 - 0.8` | Lower = smoother, higher = responsive |
| `input.terminal_mode` | `false` | `true/false` | Use Ctrl+Shift for copy/paste |
| `appearance.theme` | `"dark"` | `dark`, `light`, `oled` | Future themes |
| `appearance.candidate_count` | `5` | `3 - 8` | Candidates shown in bar |
| `advanced.learning_enabled` | `true` | `true/false` | Learn new words from usage |
| `advanced.debug_mode` | `false` | `true/false` | Enable verbose logging |

---

## 3. Swipe Sensitivity Tuning

### Tunable Parameters

The swipe system exposes these user-adjustable parameters:

```cpp
struct SwipeConfig {
    bool enabled = true;
    float sensitivity = 1.0f;      // Movement multiplier
    int deadzone_px = 12;          // Pixels before swipe starts
    int sample_rate_hz = 120;      // Target sampling frequency
    float smoothing_alpha = 0.35f; // EMA smoothing factor
    int min_path_samples = 3;      // Minimum points for valid swipe
};
```

### Sensitivity Presets

For UI simplicity, offer presets with underlying parameter mapping:

| Preset | Sensitivity | Deadzone | Smoothing | Use Case |
|--------|-------------|----------|-----------|----------|
| **Low** | 0.7 | 18px | 0.25 | Large/imprecise input |
| **Medium** (default) | 1.0 | 12px | 0.35 | Balanced |
| **High** | 1.3 | 8px | 0.45 | Precise trackpad users |
| **Custom** | User-defined | User-defined | User-defined | Power users |

### Calibration Flow (Future v0.4)

```
┌─────────────────────────────────────────────────────────┐
│          SWIPE CALIBRATION                               │
│                                                          │
│  Swipe the word "the" three times:                       │
│                                                          │
│  ┌──────────────────────────────────────────────────┐   │
│  │  [T][H][E]  ← swipe path visualization            │   │
│  └──────────────────────────────────────────────────┘   │
│                                                          │
│  Attempt 1/3: ✓ the                                      │
│  Attempt 2/3: ✓ the                                      │
│  Attempt 3/3: waiting...                                 │
│                                                          │
│  [Cancel]                        [Apply & Save]          │
└─────────────────────────────────────────────────────────┘
```

---

## 4. Learning & Persistence Store

### Learning Data Model

```json
// learning.json
{
  "version": 1,
  "updated_at": "2025-12-29T00:00:00Z",
  
  "word_boosts": {
    "fcitx": 50,
    "steamos": 45,
    "plasma": 40
  },
  
  "bigram_boosts": {
    "th": 5,
    "he": 4,
    "in": 3
  },
  
  "corrections": {
    "teh": "the",
    "recieve": "receive"
  },
  
  "blocked_words": [
    "profanity1",
    "profanity2"
  ]
}
```

### User Dictionary: `user-dictionary.txt`

Plain text, one word per line. Survives upgrades, easy to edit:

```
# Magic Keyboard User Dictionary
# Lines starting with # are comments
# Add words that should always be candidates

Hagglesworth
SteamOS
Fcitx5
KDE
```

### Frequency Adjustments: `word-frequencies.tsv`

Tab-separated word + adjustment delta:

```
fcitx	+50
plasma	+30
the	+10
```

### Learning Signals

When to boost a word's frequency:

| Signal | Boost | Rationale |
|--------|-------|-----------|
| User selects candidate | +5 | Explicit selection |
| User types word after rejection | +10 | Correction behavior |
| Word added to user dictionary | +100 | Explicit addition |
| Word committed via swipe (top candidate) | +2 | Implicit acceptance |

When to demote/block:

| Signal | Action | Rationale |
|--------|--------|-----------|
| Candidate shown but never selected (10× shows) | -1 per excess show | User consistently ignores |
| User deletes word after commit | Mark for review | Possible error |
| Explicit block in settings | Block forever | User preference |

---

## 5. Reset Mechanisms

### Reset Levels

| Level | Action | Command | Effect |
|-------|--------|---------|--------|
| **Soft Reset** | Clear session state | `magickeyboardctl reset-session` | Clears current swipe, candidate state |
| **Config Reset** | Delete config.json | `rm ~/.config/magic-keyboard/config.json` | Returns to defaults on restart |
| **Learning Reset** | Delete learning data | `magickeyboardctl reset-learning` | Forgets all learned words |
| **Full Reset** | Delete all user data | `rm -rf ~/.config/magic-keyboard ~/.local/share/magic-keyboard` | Complete factory reset |

### Safe Reset Implementation

```cpp
// In engine: process reset commands via IPC
void MagicKeyboardEngine::processResetCommand(const std::string& level) {
    if (level == "session") {
        candidateMode_ = false;
        currentCandidates_.clear();
        sendToUI("{\"type\":\"swipe_candidates\",\"candidates\":[]}");
        MKLOG(Info) << "Session reset complete";
    }
    else if (level == "learning") {
        // Atomic: write empty, then rename
        std::string path = getLearningPath();
        std::string tmp = path + ".tmp";
        std::ofstream f(tmp);
        f << "{\"version\":1,\"word_boosts\":{},\"corrections\":{}}";
        f.close();
        std::rename(tmp.c_str(), path.c_str());
        loadLearning(); // Reload
        MKLOG(Info) << "Learning data reset";
    }
}
```

---

## 6. Config Loading Strategy

### Load Order (Layered)

```
1. Compile-time defaults (hardcoded)
         ↓ overridden by
2. System config (/usr/share/magic-keyboard/config.json)
         ↓ overridden by
3. Local install (/usr/local/share/magic-keyboard/config.json)
         ↓ overridden by
4. User config (~/.config/magic-keyboard/config.json)
         ↓ overridden by
5. Environment variables (MAGIC_KEYBOARD_*)
         ↓ overridden by
6. Runtime IPC commands
```

### C++ Implementation Sketch

```cpp
class ConfigManager {
public:
    void loadConfig() {
        // Start with hardcoded defaults
        config_ = getDefaultConfig();
        
        // Layer system configs
        for (const auto& path : getSystemConfigPaths()) {
            if (auto parsed = tryLoadJson(path)) {
                mergeConfig(*parsed);
                MKLOG(Info) << "Loaded config from: " << path;
            }
        }
        
        // User config (highest priority file)
        std::string userPath = getUserConfigPath();
        if (auto parsed = tryLoadJson(userPath)) {
            mergeConfig(*parsed);
            MKLOG(Info) << "Loaded user config from: " << userPath;
        }
        
        // Environment overrides
        applyEnvironmentOverrides();
        
        // Validate final config
        validateAndClamp();
    }
    
    void saveConfig() {
        std::string path = getUserConfigPath();
        std::string tmp = path + ".tmp";
        
        // Ensure directory exists
        createDirectories(getParentPath(path));
        
        // Write to temp file
        std::ofstream f(tmp);
        f << serializeConfig();
        f.close();
        
        // Atomic rename
        if (std::rename(tmp.c_str(), path.c_str()) != 0) {
            MKLOG(Error) << "Config save failed: " << strerror(errno);
        }
    }

private:
    std::vector<std::string> getSystemConfigPaths() {
        return {
            "/usr/share/magic-keyboard/config.json",
            "/usr/local/share/magic-keyboard/config.json"
        };
    }
    
    std::string getUserConfigPath() {
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        std::string base = xdg ? xdg : (std::string(std::getenv("HOME")) + "/.config");
        return base + "/magic-keyboard/config.json";
    }
    
    void validateAndClamp() {
        // Clamp sensitivity to valid range
        config_.swipe.sensitivity = std::clamp(config_.swipe.sensitivity, 0.5f, 2.0f);
        config_.swipe.deadzone_px = std::clamp(config_.swipe.deadzone_px, 6, 24);
        config_.appearance.candidate_count = std::clamp(config_.appearance.candidate_count, 3, 8);
    }
};
```

### Environment Variable Overrides

For debugging and automation:

| Variable | Config Path | Example |
|----------|-------------|---------|
| `MAGIC_KEYBOARD_DEBUG` | `advanced.debug_mode` | `1` or `true` |
| `MAGIC_KEYBOARD_TERMINAL_MODE` | `input.terminal_mode` | `1` or `true` |
| `MAGIC_KEYBOARD_SWIPE_ENABLED` | `swipe.enabled` | `0` or `false` |
| `MAGIC_KEYBOARD_LAYOUT` | `layout.name` | `dvorak` |

---

## 7. Atomic Write Safety

### The Problem

Steam Deck users may:
- Put device to sleep mid-write
- Run out of battery unexpectedly
- Force-reboot via power button

A corrupted config file should not break the keyboard.

### The Solution: Atomic Rename Pattern

```cpp
void atomicWriteJson(const std::string& path, const std::string& content) {
    std::string tmp = path + ".tmp." + std::to_string(getpid());
    
    // 1. Write to temp file
    std::ofstream f(tmp);
    if (!f.is_open()) {
        throw std::runtime_error("Cannot create temp file");
    }
    f << content;
    f.flush();
    fsync(fileno(f.rdbuf()->fd()));  // Force to disk
    f.close();
    
    // 2. Atomic rename (POSIX guarantees atomicity on same filesystem)
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        unlink(tmp.c_str());  // Clean up
        throw std::runtime_error("Rename failed");
    }
}
```

### Recovery from Corruption

```cpp
std::optional<json> tryLoadJson(const std::string& path) {
    try {
        std::ifstream f(path);
        if (!f.is_open()) return std::nullopt;
        
        std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
        return json::parse(content);
    }
    catch (const json::exception& e) {
        MKLOG(Warn) << "Corrupt config at " << path << ": " << e.what();
        
        // Backup corrupt file for debugging
        std::rename(path.c_str(), (path + ".corrupt." + timestamp()).c_str());
        
        return std::nullopt;  // Will fall back to defaults
    }
}
```

---

## 8. Fcitx5 Integration

### Native Fcitx5 Config (Future)

Fcitx5 has its own config system (`fcitx5-configtool`). For v0.4+, we may integrate:

```ini
# ~/.config/fcitx5/conf/magickeyboard.conf
[General]
SwipeSensitivity=1.0
TerminalMode=False
Theme=dark

[Appearance]
CandidateCount=5
ShowSwipeTrail=True
```

### Why Not Use Fcitx5 Config Now

| Pro | Con |
|-----|-----|
| Integration with fcitx5-configtool | Requires GUI addon development |
| Consistent with other IMs | Learning/dictionary still needs custom storage |
| | More complex to implement |

**Decision**: Use standalone config.json for v0.1-v0.3. Migrate to Fcitx5 native config in v0.4 if there's demand.

---

## 9. IPC Protocol Extensions

### Config Commands

```json
// Request current config
{"type":"get_config"}

// Response
{"type":"config","data":{...full config...}}

// Update single setting
{"type":"set_config","path":"swipe.sensitivity","value":1.2}

// Response
{"type":"config_updated","path":"swipe.sensitivity","value":1.2}

// Save to disk
{"type":"save_config"}

// Reset commands
{"type":"reset","level":"session"}
{"type":"reset","level":"learning"}
```

### UI Settings Panel Integration

The UI can request and modify config via socket:

```qml
// SettingsPanel.qml
function loadSettings() {
    socket.send('{"type":"get_config"}');
}

function updateSensitivity(value) {
    socket.send(JSON.stringify({
        type: "set_config",
        path: "swipe.sensitivity",
        value: value
    }));
}

Connections {
    target: socket
    function onMessageReceived(msg) {
        if (msg.type === "config") {
            sensitivitySlider.value = msg.data.swipe.sensitivity;
            // ... populate other fields
        }
    }
}
```

---

## 10. File Format Decisions

### Why JSON (Not TOML, YAML, INI)

| Format | Pros | Cons | Decision |
|--------|------|------|----------|
| JSON | Universal, Qt/C++ native support, schema validation | No comments, verbose | **Chosen** |
| TOML | Human-friendly, comments | No Qt native parser | Future option |
| YAML | Very readable | Parsing complexity, security concerns | Rejected |
| INI | Simple | No nested structures | Too limited |

### JSON with Comments Convention

Since JSON doesn't support comments, we use a convention:

```json
{
  "_comment": "See https://magic-keyboard.dev/docs/config for all options",
  "version": 1,
  "swipe": {
    "_comment": "Sensitivity: 0.5 (low) to 2.0 (high)",
    "sensitivity": 1.0
  }
}
```

The parser ignores any key starting with `_`.

---

## 11. Security Considerations

### Permission Model

```bash
# Config directory
drwx------  ~/.config/magic-keyboard/          # 700: user only

# Config files
-rw-------  ~/.config/magic-keyboard/config.json  # 600: user only

# User dictionary (may want to share)
-rw-r--r--  ~/.local/share/magic-keyboard/user-dictionary.txt  # 644
```

### Input Validation

All loaded config values MUST be validated:

```cpp
void validateConfig(Config& cfg) {
    // Numeric ranges
    cfg.swipe.sensitivity = std::clamp(cfg.swipe.sensitivity, 0.5f, 2.0f);
    cfg.swipe.deadzone_px = std::clamp(cfg.swipe.deadzone_px, 1, 100);
    
    // Enum values
    if (!isValidLayout(cfg.layout.name)) {
        MKLOG(Warn) << "Unknown layout: " << cfg.layout.name << ", using qwerty";
        cfg.layout.name = "qwerty";
    }
    
    // Path traversal prevention
    if (cfg.advanced.dictionary_path) {
        std::string path = *cfg.advanced.dictionary_path;
        if (path.find("..") != std::string::npos) {
            MKLOG(Error) << "Invalid dictionary path (contains ..): " << path;
            cfg.advanced.dictionary_path = std::nullopt;
        }
    }
}
```

---

## 12. Migration & Upgrade Path

### Version Migration Table

| From | To | Migration Steps |
|------|----|--------------------|
| No config | v1 | Create with defaults |
| v1 | v2 | Rename `foo` → `bar`, add new fields with defaults |
| Corrupt | Any | Backup corrupt file, start fresh |

### Migration Code Template

```cpp
json migrateConfig(json config) {
    int version = config.value("version", 0);
    
    if (version == 0) {
        // Pre-versioned config: assume v1 structure
        version = 1;
    }
    
    if (version == 1) {
        // v1 → v2 migration (example)
        // if (config.contains("old_key")) {
        //     config["new_key"] = config["old_key"];
        //     config.erase("old_key");
        // }
        // version = 2;
    }
    
    config["version"] = version;
    return config;
}
```

---

## 13. Testing Strategy

### Unit Tests

```cpp
// test_config.cpp

TEST(ConfigManager, LoadsDefaults) {
    ConfigManager mgr;
    mgr.loadConfig();  // No files exist
    
    EXPECT_EQ(mgr.get<float>("swipe.sensitivity"), 1.0f);
    EXPECT_EQ(mgr.get<bool>("swipe.enabled"), true);
    EXPECT_EQ(mgr.get<std::string>("layout.name"), "qwerty");
}

TEST(ConfigManager, RespectsUserOverride) {
    // Create temp config file
    TempFile cfg("{\"swipe\":{\"sensitivity\":1.5}}");
    setenv("XDG_CONFIG_HOME", cfg.dir().c_str(), 1);
    
    ConfigManager mgr;
    mgr.loadConfig();
    
    EXPECT_FLOAT_EQ(mgr.get<float>("swipe.sensitivity"), 1.5f);
}

TEST(ConfigManager, ClampsOutOfRange) {
    TempFile cfg("{\"swipe\":{\"sensitivity\":999.0}}");
    
    ConfigManager mgr;
    mgr.loadConfig();
    
    EXPECT_FLOAT_EQ(mgr.get<float>("swipe.sensitivity"), 2.0f);  // Clamped
}

TEST(ConfigManager, SurvivesCorruption) {
    TempFile cfg("this is not valid json {{{");
    
    ConfigManager mgr;
    mgr.loadConfig();  // Should not throw
    
    // Falls back to defaults
    EXPECT_EQ(mgr.get<float>("swipe.sensitivity"), 1.0f);
}
```

### Integration Tests

```bash
#!/bin/bash
# test_config_persistence.sh

# Setup
export XDG_CONFIG_HOME=$(mktemp -d)
CONFIG_FILE="$XDG_CONFIG_HOME/magic-keyboard/config.json"

# Test 1: First run creates no config (uses defaults)
./magickeyboardctl get-config > /dev/null
[ ! -f "$CONFIG_FILE" ] && echo "PASS: No config created on read"

# Test 2: Setting a value creates config
./magickeyboardctl set-config swipe.sensitivity 1.5
[ -f "$CONFIG_FILE" ] && echo "PASS: Config created on write"

# Test 3: Value persists
VALUE=$(./magickeyboardctl get-config swipe.sensitivity)
[ "$VALUE" = "1.5" ] && echo "PASS: Value persisted"

# Cleanup
rm -rf "$XDG_CONFIG_HOME"
```

---

## 14. Implementation Roadmap

### v0.3: Minimal Config

| Deliverable | Description |
|-------------|-------------|
| ConfigManager class | Load/save/merge logic |
| Default config.json | Document all options |
| Environment overrides | Debug mode, terminal mode |
| CLI: `magickeyboardctl config` | Get/set commands |

### v0.4: Settings UI

| Deliverable | Description |
|-------------|-------------|
| Settings panel in QML | Full config editor |
| Swipe calibration wizard | Guided sensitivity tuning |
| Export/import | Backup/restore settings |
| Fcitx5 config integration | Native settings UI |

### v0.5: Learning System

| Deliverable | Description |
|-------------|-------------|
| Learning.json persistence | Word frequency boosts |
| User dictionary editor | Add/remove words in UI |
| Correction learning | Remember "teh" → "the" |
| Privacy controls | Per-app learning disable |

---

## 15. Open Questions (For Future Consideration)

1. **Cloud Sync**: Should learning data sync across devices? Privacy implications?

2. **Per-App Profiles**: Different settings for different applications? (e.g., games vs. text editors)

3. **Locale-Specific Defaults**: Should `en_GB` users get different defaults than `en_US`?

4. **Backup Strategy**: Should we auto-backup config before major version upgrades?

5. **Telemetry**: Opt-in usage statistics to improve defaults? (Probably no for privacy)

---

## Revision History

| Date | Change |
|------|--------|
| 2025-12-29 | Initial Settings & Persistence Agent design specification |
