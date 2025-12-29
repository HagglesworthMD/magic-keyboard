/**
 * Magic Keyboard - Fcitx5 Input Method Engine
 * v0.1: Focus-driven show/hide + click-to-commit via Unix socket
 */
#include "magickeyboard.h"
#include "protocol.h"

#include <fcitx-utils/event.h>
#include <fcitx-utils/log.h>
#include <fcitx/event.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>

namespace magickeyboard {

FCITX_DEFINE_LOG_CATEGORY(magickeyboard_log, "magickeyboard");
#define MKLOG(level) FCITX_LOGC(magickeyboard_log, level)

fcitx::AddonInstance *
MagicKeyboardFactory::create(fcitx::AddonManager *manager) {
  return new MagicKeyboardEngine(manager->instance());
}

MagicKeyboardEngine::MagicKeyboardEngine(fcitx::Instance *instance)
    : instance_(instance) {
  MKLOG(Info) << "Magic Keyboard engine starting";

  loadLayout("qwerty");
  loadDictionary();
  startSocketServer();

  // Initialize toggle timer to allow immediate first toggle
  lastToggleTime_ = std::chrono::steady_clock::now() - std::chrono::seconds(1);

  // Watch for actual text field focus changes
  focusInConn_ = instance_->watchEvent(
      fcitx::EventType::InputContextFocusIn, fcitx::EventWatcherPhase::Default,
      [this](fcitx::Event &event) {
        if (shuttingDown_)
          return;

        auto &icEvent = static_cast<fcitx::FocusInEvent &>(event);
        auto *ic = icEvent.inputContext();
        if (!ic)
          return;

        handleFocusIn(ic);
      });

  focusOutConn_ = instance_->watchEvent(
      fcitx::EventType::InputContextFocusOut, fcitx::EventWatcherPhase::Default,
      [this](fcitx::Event &event) {
        if (shuttingDown_)
          return;

        auto &icEvent = static_cast<fcitx::FocusOutEvent &>(event);
        auto *ic = icEvent.inputContext();
        if (!ic)
          return;

        handleFocusOut(ic);
      });

  startWatchdog();

  MKLOG(Info) << "Magic Keyboard engine ready";
}

MagicKeyboardEngine::~MagicKeyboardEngine() {
  MKLOG(Info) << "MagicKeyboard: shutdown begin";

  shuttingDown_ = true;

  // Kill connections first to stop callbacks
  focusInConn_.reset();
  focusOutConn_.reset();
  debounceTimer_.reset(); // Cancel any pending state transitions
  watchdogTimer_.reset();

  stopSocketServer();

  if (uiPid_ > 0) {
    kill(uiPid_, SIGTERM);
    // Let init/systemd reap orphan
  }

  MKLOG(Info) << "MagicKeyboard: shutdown end";
}

void MagicKeyboardEngine::reloadConfig() {}

std::vector<fcitx::InputMethodEntry> MagicKeyboardEngine::listInputMethods() {
  std::vector<fcitx::InputMethodEntry> result;

  auto entry = fcitx::InputMethodEntry("magic-keyboard", "Magic Keyboard", "en",
                                       "magickeyboard");
  entry.setLabel("MK");
  entry.setIcon("input-keyboard");

  result.push_back(std::move(entry));
  MKLOG(Info) << "Registered input method: magic-keyboard";

  return result;
}

void MagicKeyboardEngine::activate(const fcitx::InputMethodEntry &,
                                   fcitx::InputContextEvent &event) {
  currentIC_ = event.inputContext();
  MKLOG(Debug) << "activate()";
}

void MagicKeyboardEngine::deactivate(const fcitx::InputMethodEntry &,
                                     fcitx::InputContextEvent &) {
  MKLOG(Debug) << "deactivate()";
  currentIC_ = nullptr;
}

void MagicKeyboardEngine::keyEvent(const fcitx::InputMethodEntry &,
                                   fcitx::KeyEvent &keyEvent) {
  keyEvent.filterAndAccept();
}

void MagicKeyboardEngine::reset(const fcitx::InputMethodEntry &,
                                fcitx::InputContextEvent &) {
  candidateMode_ = false;
  currentCandidates_.clear();
  sendToUI("{\"type\":\"swipe_candidates\",\"candidates\":[]}\n");
}

// Check if this context should show the keyboard
// Returns: 0=no, 1=yes, sets reason string
int MagicKeyboardEngine::shouldShowKeyboard(fcitx::InputContext *ic,
                                            std::string &reason) {
  if (!ic) {
    reason = "null-ic";
    return 0;
  }

  // Must be using Magic Keyboard (per-IC check is authoritative)
  const auto *entry = instance_->inputMethodEntry(ic);
  bool isOurs = (entry && entry->addon() == "magickeyboard");

  // Fallback only if entry is null
  if (!entry) {
    std::string globalIM = instance_->currentInputMethod();
    isOurs = (globalIM == "magic-keyboard");
  }

  if (!isOurs) {
    reason = "other-im";
    return 0;
  }

  // Check capabilities - don't show for password fields
  auto caps = ic->capabilityFlags();
  if (caps.test(fcitx::CapabilityFlag::Password)) {
    reason = "password";
    return 0;
  }

  // Respect NoOnScreenKeyboard hint (Qt apps can request this)
  if (caps.test(fcitx::CapabilityFlag::NoOnScreenKeyboard)) {
    reason = "no-osk-hint";
    return 0;
  }

  // Don't show for sensitive fields (privacy mode)
  if (caps.test(fcitx::CapabilityFlag::Sensitive)) {
    reason = "sensitive";
    return 0;
  }

  reason = "ok";
  return 1;
}

// === Focus & Visibility Control State Machine (Agent #2) ===
// Debounced focus handling prevents flicker from rapid widget focus changes

void MagicKeyboardEngine::handleFocusIn(fcitx::InputContext *ic) {
  if (shuttingDown_)
    return;

  std::string program = ic ? ic->program() : "?";
  std::string reason;
  int show = shouldShowKeyboard(ic, reason);

  MKLOG(Info) << "FocusIn: " << program << " show=" << show << " (" << reason
              << ") state=" << static_cast<int>(visibilityState_);

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

void MagicKeyboardEngine::handleFocusOut(fcitx::InputContext *ic) {
  if (shuttingDown_)
    return;

  std::string program = ic ? ic->program() : "?";
  MKLOG(Info) << "FocusOut: " << program
              << " state=" << static_cast<int>(visibilityState_);

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

// === Debounce Helper Methods ===

void MagicKeyboardEngine::scheduleDebounce(VisibilityState target,
                                           int delayMs) {
  cancelDebounce();

  debounceTimer_ = instance_->eventLoop().addTimeEvent(
      CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + delayMs * 1000, 0,
      [this, target](fcitx::EventSourceTime *, uint64_t) {
        if (shuttingDown_)
          return false;
        executeTransition(target);
        return false; // One-shot timer
      });
}

void MagicKeyboardEngine::cancelDebounce() { debounceTimer_.reset(); }

void MagicKeyboardEngine::executeTransition(VisibilityState target) {
  MKLOG(Debug) << "ExecuteTransition to " << static_cast<int>(target);

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

void MagicKeyboardEngine::sendToUI(const std::string &msg) {
  if (clients_.empty()) {
    return;
  }

  for (auto const &[fd, client] : clients_) {
    ssize_t n = write(fd, msg.c_str(), msg.size());
    if (n < 0) {
      if (errno == EPIPE) {
        // Normal for fire-and-forget control clients
        MKLOG(Debug) << "Send failed (EPIPE) to fd " << fd;
      } else {
        MKLOG(Warn) << "Send failed to fd " << fd << ": " << strerror(errno);
      }
    }
  }
}

void MagicKeyboardEngine::ensureUIRunning() {
  if (!clients_.empty())
    return;
  if (uiSpawnPending_)
    return;

  if (uiPid_ > 0) {
    int status;
    pid_t result = waitpid(uiPid_, &status, WNOHANG);
    if (result == 0)
      return;
    uiPid_ = 0;
  }

  launchUI();
}

bool MagicKeyboardEngine::isTerminal(const std::string &program) {
  static const std::vector<std::string> terminals = {"konsole",
                                                     "gnome-terminal",
                                                     "alacritty",
                                                     "kitty",
                                                     "foot",
                                                     "xterm",
                                                     "terminator",
                                                     "tilix",
                                                     "terminology",
                                                     "wezterm",
                                                     "hyper",
                                                     "st",
                                                     "urxvt",
                                                     "mlterm",
                                                     "sakura",
                                                     "termite",
                                                     "cool-retro-term",
                                                     "yakuake",
                                                     "guake",
                                                     "tilda",
                                                     "qterminal"};

  std::string progLower = program;
  std::transform(progLower.begin(), progLower.end(), progLower.begin(),
                 ::tolower);

  for (const auto &t : terminals) {
    if (progLower.find(t) != std::string::npos) {
      return true;
    }
  }
  return false;
}

void MagicKeyboardEngine::handleShortcutAction(const std::string &action) {
  auto *ic = instance_->inputContextManager().lastFocusedInputContext();
  if (!ic || !ic->hasFocus()) {
    MKLOG(Warn) << "ShortcutAction '" << action << "' but no active IC";
    return;
  }

  // Determine if we need terminal-mode shortcuts
  std::string program = ic->program();
  bool useShift =
      isTerminal(program) && (action == "copy" || action == "paste");

  // Check capability restrictions
  auto caps = ic->capabilityFlags();

  // Password fields: allow paste, block copy/cut for security
  if (caps.test(fcitx::CapabilityFlag::Password)) {
    if (action == "copy" || action == "cut") {
      MKLOG(Info) << "Blocked " << action << " in password field";
      return;
    }
  }

  // Map action to key symbol
  fcitx::KeySym sym = FcitxKey_None;
  if (action == "copy") {
    sym = FcitxKey_c;
  } else if (action == "paste") {
    sym = FcitxKey_v;
  } else if (action == "cut") {
    sym = FcitxKey_x;
  } else if (action == "selectall") {
    sym = FcitxKey_a;
  } else if (action == "undo") {
    sym = FcitxKey_z;
  } else if (action == "redo") {
    sym = FcitxKey_y;
  } else {
    MKLOG(Warn) << "Unknown shortcut action: " << action;
    return;
  }

  // Build key states
  fcitx::KeyStates states = fcitx::KeyState::Ctrl;
  if (useShift) {
    states |= fcitx::KeyState::Shift;
  }

  // Create key with modifiers
  fcitx::Key key(sym, states);

  // Forward key-down then key-up
  ic->forwardKey(key, false);
  ic->forwardKey(key, true);

  MKLOG(Info) << "Shortcut: " << action << " -> "
              << (useShift ? "Ctrl+Shift+" : "Ctrl+")
              << static_cast<char>(sym - FcitxKey_a + 'A')
              << " program=" << program;
}

void MagicKeyboardEngine::handleKeyPress(const std::string &key) {
  if (!currentIC_) {
    MKLOG(Warn) << "Key but no active IC";
    return;
  }

  MKLOG(Debug) << "Commit: " << key;

  if (candidateMode_) {
    if (key == "space") {
      if (!currentCandidates_.empty()) {
        currentIC_->commitString(currentCandidates_[0].word + " ");
        MKLOG(Info) << "CommitTop word=" << currentCandidates_[0].word
                    << " space=1";
      } else {
        currentIC_->commitString(" ");
      }
      candidateMode_ = false;
      currentCandidates_.clear();
      sendToUI("{\"type\":\"swipe_candidates\",\"candidates\":[]}\n");
      return;
    } else if (key == "backspace") {
      candidateMode_ = false;
      currentCandidates_.clear();
      sendToUI("{\"type\":\"swipe_candidates\",\"candidates\":[]}\n");
      return;
    } else if (key == "enter") {
      if (!currentCandidates_.empty()) {
        currentIC_->commitString(currentCandidates_[0].word);
        MKLOG(Info) << "CommitTop (Enter) word=" << currentCandidates_[0].word
                    << " space=0";
      }
      candidateMode_ = false;
      currentCandidates_.clear();
      sendToUI("{\"type\":\"swipe_candidates\",\"candidates\":[]}\n");
      // Fall through to normal enter handling below
    } else {
      // Implicit commit for letters
      if (!currentCandidates_.empty()) {
        currentIC_->commitString(currentCandidates_[0].word);
        MKLOG(Info) << "CommitTop (Implicit) word="
                    << currentCandidates_[0].word << " space=0";
      }
      candidateMode_ = false;
      currentCandidates_.clear();
      sendToUI("{\"type\":\"swipe_candidates\",\"candidates\":[]}\n");
    }
  }

  if (key == "backspace") {
    currentIC_->forwardKey(fcitx::Key(FcitxKey_BackSpace), false);
    currentIC_->forwardKey(fcitx::Key(FcitxKey_BackSpace), true);
  } else if (key == "enter") {
    currentIC_->forwardKey(fcitx::Key(FcitxKey_Return), false);
    currentIC_->forwardKey(fcitx::Key(FcitxKey_Return), true);
  } else if (key == "space") {
    currentIC_->commitString(" ");
  } else {
    currentIC_->commitString(key);
  }
}

void MagicKeyboardEngine::loadLayout(const std::string &layoutName) {
  keys_.clear();
  std::vector<std::string> searchPaths = {
      "data/layouts/" + layoutName + ".json",
      "/usr/local/share/magic-keyboard/layouts/" + layoutName + ".json",
      "/usr/share/magic-keyboard/layouts/" + layoutName + ".json"};

  std::ifstream f;
  std::string foundPath;
  for (const auto &p : searchPaths) {
    f.open(p);
    if (f.is_open()) {
      foundPath = p;
      break;
    }
  }

  if (!f.is_open()) {
    MKLOG(Error) << "Failed to find layout: " << layoutName;
    return;
  }

  MKLOG(Info) << "Loading layout from: " << foundPath;

  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());

  double keyUnit = 60.0;
  double keyHeight = 50.0;
  double spacing = 6.0;

  auto findVal = [](const std::string &container,
                    const std::string &key) -> std::string {
    size_t kp = container.find("\"" + key + "\":");
    if (kp == std::string::npos)
      return "";
    size_t start =
        container.find_first_of("0123456789.-", kp + key.length() + 2);
    if (start == std::string::npos)
      return "";
    size_t end = container.find_first_not_of("0123456789.", start);
    return container.substr(start, end - start);
  };

  size_t rowsStart = content.find("\"rows\"");
  if (rowsStart == std::string::npos)
    return;

  int currentRowY = 0;
  double currentRowOffset = 0.0;
  size_t pos = rowsStart;

  while ((pos = content.find("{", pos)) != std::string::npos) {
    size_t end = content.find("}", pos);
    if (end == std::string::npos)
      break;
    std::string obj = content.substr(pos, end - pos + 1);

    // Is it a row? (has "y" and "keys")
    std::string yStr = findVal(obj, "y");
    if (!yStr.empty() && obj.find("\"keys\"") != std::string::npos) {
      currentRowY = std::stoi(yStr);
      currentRowOffset = 0.0;
      std::string offStr = findVal(obj, "offset");
      if (!offStr.empty())
        currentRowOffset = std::stod(offStr);
    } else {
      // Is it a key? (has "x" and "w")
      std::string xStr = findVal(obj, "x");
      std::string wStr = findVal(obj, "w");
      if (!xStr.empty() && !wStr.empty()) {
        Key k;
        // Parse code: "code": "..."
        size_t cp = obj.find("\"code\":");
        if (cp != std::string::npos) {
          size_t s = obj.find("\"", cp + 7);
          if (s != std::string::npos) {
            size_t e = obj.find("\"", s + 1);
            if (e != std::string::npos)
              k.id = obj.substr(s + 1, e - s - 1);
          }
        }

        double kx = std::stod(xStr) + currentRowOffset;
        double kw = std::stod(wStr);
        k.r.x = kx * (keyUnit + spacing);
        k.r.y = currentRowY * (keyHeight + spacing);
        k.r.w = kw * keyUnit + (kw > 1 ? (kw - 1) * spacing : 0);
        k.r.h = keyHeight;
        k.center.x = k.r.x + k.r.w / 2.0;
        k.center.y = k.r.y + k.r.h / 2.0;
        keys_.push_back(k);
      }
    }
    pos = pos + 1; // Move to next possible object start
  }

  MKLOG(Info) << "Layout loaded: " << keys_.size() << " keys";
}

std::vector<std::string>
MagicKeyboardEngine::mapPathToSequence(const std::vector<Point> &path) {
  if (path.empty() || keys_.empty())
    return {};

  std::vector<std::string> rawSequence;
  const Key *currentKey = nullptr;
  int consecutiveSamples = 0;

  for (const auto &pt : path) {
    const Key *bestKey = nullptr;
    double bestDistSq = 1e18;

    // Inside-rect priority
    for (const auto &k : keys_) {
      if (k.r.contains(pt)) {
        bestKey = &k;
        break;
      }
      double dx = k.center.x - pt.x;
      double dy = k.center.y - pt.y;
      double d2 = dx * dx + dy * dy;
      if (d2 < bestDistSq) {
        bestDistSq = d2;
        bestKey = &k;
      }
    }

    if (!bestKey)
      continue;

    // Hysteresis
    if (currentKey == nullptr) {
      currentKey = bestKey;
      consecutiveSamples = 1;
      rawSequence.push_back(currentKey->id);
    } else if (bestKey != currentKey) {
      bool accept = false;

      // 1. Inside rect win
      if (bestKey->r.contains(pt)) {
        accept = true;
      } else {
        // 2. Strong distance win (0.72 ratio) AND minimum absolute gap (6px)
        double dx_cur = currentKey->center.x - pt.x;
        double dy_cur = currentKey->center.y - pt.y;
        double d2_cur = dx_cur * dx_cur + dy_cur * dy_cur;
        if (bestDistSq < d2_cur * (0.72 * 0.72) &&
            (std::sqrt(d2_cur) - std::sqrt(bestDistSq)) > 6.0) {
          accept = true;
        }
        // 3. Consecutive samples win
        else if (consecutiveSamples >= 2) {
          // Wait, this is inverted. If we have been on currentKey for >= 2,
          // we are stable. If BEST is different, we check if it's been best
          // for >= 2.
        }
      }

      // Re-implementing correctly: require 2 samples for candidate if not
      // dominant
      static const Key *candidateKey = nullptr;
      static int candidateCount = 0;

      if (accept) {
        currentKey = bestKey;
        rawSequence.push_back(currentKey->id);
        candidateKey = nullptr;
        candidateCount = 0;
      } else {
        if (bestKey == candidateKey) {
          candidateCount++;
        } else {
          candidateKey = bestKey;
          candidateCount = 1;
        }

        if (candidateCount >= 2) {
          currentKey = bestKey;
          rawSequence.push_back(currentKey->id);
          candidateKey = nullptr;
          candidateCount = 0;
        }
      }
    } else {
      consecutiveSamples++; // Still on same key
    }
  }

  // Final Processing
  if (rawSequence.empty())
    return {};

  // 1. Collapse duplicates (AAAA -> A)
  std::vector<std::string> collapsed;
  for (const auto &s : rawSequence) {
    if (collapsed.empty() || s != collapsed.back()) {
      collapsed.push_back(s);
    }
  }

  // 2. Remove A-B-A bounces where B is very short (dwell=1)
  // Since we don't have dwell counts in 'collapsed', we'd need to do this
  // during collapsing. Let's refine the collapsing.
  std::vector<std::pair<std::string, int>> dwells;
  for (const auto &s : rawSequence) {
    if (dwells.empty() || s != dwells.back().first) {
      dwells.push_back({s, 1});
    } else {
      dwells.back().second++;
    }
  }

  std::vector<std::string> finalSeq;
  for (size_t i = 0; i < dwells.size(); ++i) {
    // ABA bounce check
    if (i > 0 && i < dwells.size() - 1) {
      if (dwells[i - 1].first == dwells[i + 1].first && dwells[i].second < 2) {
        continue; // Skip B
      }
    }
    finalSeq.push_back(dwells[i].first);
  }

  // Re-collapse in case bounce removal created new duplicates
  std::vector<std::string> result;
  for (const auto &s : finalSeq) {
    if (result.empty() || s != result.back()) {
      result.push_back(s);
    }
  }

  return result;
}

void MagicKeyboardEngine::processLine(const std::string &line, int clientFd) {
  if (line.find("\"type\":\"key\"") != std::string::npos) {
    auto pos = line.find("\"text\":\"");
    if (pos != std::string::npos) {
      pos += 8;
      auto end = line.find("\"", pos);
      if (end != std::string::npos) {
        handleKeyPress(line.substr(pos, end - pos));
      }
    }
  } else if (line.find("\"type\":\"commit_candidate\"") != std::string::npos) {
    auto pos = line.find("\"text\":\"");
    if (pos != std::string::npos) {
      pos += 8;
      auto end = line.find("\"", pos);
      if (end != std::string::npos) {
        std::string text = line.substr(pos, end - pos);
        auto *ic = instance_->inputContextManager().lastFocusedInputContext();
        if (ic && ic->hasFocus()) {
          ic->commitString(text);
          MKLOG(Info) << "CommitCand word=" << text << " space=0";
          candidateMode_ = false;
          currentCandidates_.clear();
          sendToUI("{\"type\":\"swipe_candidates\",\"candidates\":[]}\n");
        }
      }
    }
  } else if (line.find("\"type\":\"action\"") != std::string::npos) {
    auto pos = line.find("\"action\":\"");
    if (pos != std::string::npos) {
      pos += 10;
      auto end = line.find("\"", pos);
      if (end != std::string::npos) {
        handleShortcutAction(line.substr(pos, end - pos));
      }
    }
  } else if (line.find("\"type\":\"ui_show\"") != std::string::npos ||
             line.find("\"type\":\"ui_hide\"") != std::string::npos ||
             line.find("\"type\":\"ui_toggle\"") != std::string::npos) {
    // Sender-side throttling for toggle (100ms)
    bool isToggle = line.find("\"type\":\"ui_toggle\"") != std::string::npos;
    bool shouldSend = true;

    auto now = std::chrono::steady_clock::now();
    if (isToggle) {
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - lastToggleTime_)
                    .count();
      if (ms < 100) {
        MKLOG(Debug) << "Ignored rapid toggle (engine side)";
        shouldSend = false;
      } else {
        lastToggleTime_ = now;
      }
    }

    // Relay control messages to all clients (UI will handle)
    if (shouldSend) {
      sendToUI(line + "\n");
    }

    // ACcknowledge to the control client (Agent #4 fix)
    if (clientFd >= 0) {
      std::string ack = "{\"ok\":true}\n";
      write(clientFd, ack.c_str(), ack.size());
    }
  } else if (line.find("\"type\":\"swipe_path\"") != std::string::npos) {
    // Parse points
    std::vector<Point> path;
    size_t pts_pos = line.find("\"points\":[");
    if (pts_pos != std::string::npos) {
      size_t search = pts_pos + 10;
      while (true) {
        size_t objStart = line.find('{', search);
        if (objStart == std::string::npos)
          break;
        size_t xPos = line.find("\"x\":", objStart);
        size_t yPos = line.find("\"y\":", objStart);
        if (xPos == std::string::npos || yPos == std::string::npos)
          break;
        double x = std::stod(line.substr(xPos + 4));
        double y = std::stod(line.substr(yPos + 4));
        path.push_back({x, y});
        search = line.find('}', yPos);
        if (search == std::string::npos)
          break;
      }
    }

    if (!path.empty()) {
      auto seq = mapPathToSequence(path);
      std::string keysString;
      for (const auto &s : seq)
        keysString += s;

      auto candidates = generateCandidates(keysString, path.size());

      // Logging is handled inside generateCandidates (v0.2.3.1 spec)
      // but we add the SwipeMap style one too if needed.
      // Spec says: SwipeCand ...

      // Send keys for debug highlight
      std::string msgKeys = "{\"type\":\"swipe_keys\",\"keys\":[";
      for (size_t i = 0; i < seq.size(); ++i) {
        msgKeys += "\"" + seq[i] + "\"";
        if (i < seq.size() - 1)
          msgKeys += ",";
      }
      msgKeys += "]}\n";
      sendToUI(msgKeys);

      // Send candidates
      std::string msgCands = "{\"type\":\"swipe_candidates\",\"candidates\":[";
      for (size_t i = 0; i < candidates.size(); ++i) {
        msgCands += "{\"w\":\"" + candidates[i].word + "\"}";
        if (i < candidates.size() - 1)
          msgCands += ",";
      }
      msgCands += "],\"keys\":[";
      for (size_t i = 0; i < seq.size(); ++i) {
        msgCands += "\"" + seq[i] + "\"";
        if (i < seq.size() - 1)
          msgCands += ",";
      }
      msgCands += "]}\n";
      sendToUI(msgCands);
    }
  } else if (line.find("\"type\":\"hello\"") != std::string::npos) {
    auto pos = line.find("\"role\":\"");
    if (pos != std::string::npos) {
      pos += 8;
      auto end = line.find("\"", pos);
      if (end != std::string::npos) {
        std::string role = line.substr(pos, end - pos);
        auto it = clients_.find(clientFd);
        if (it != clients_.end()) {
          it->second->role = role;
          MKLOG(Info) << "Client " << clientFd
                      << " identified as role: " << role;
        }
      }
    }
  }
}

void MagicKeyboardEngine::startWatchdog() {
  watchdogTimer_ = instance_->eventLoop().addTimeEvent(
      CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + WATCHDOG_MS * 1000, 0,
      [this](fcitx::EventSourceTime *source, uint64_t) {
        if (shuttingDown_)
          return false;

        // Watchdog only acts when keyboard is visible or pending hide
        if (visibilityState_ == VisibilityState::Visible ||
            visibilityState_ == VisibilityState::PendingHide) {
          auto *ic = instance_->inputContextManager().lastFocusedInputContext();
          if (!ic || !ic->hasFocus()) {
            MKLOG(Info) << "Watchdog: no focused IC found, forcing hide";
            cancelDebounce();
            executeHide();
          }
        }

        source->setTime(fcitx::now(CLOCK_MONOTONIC) + WATCHDOG_MS * 1000);
        return true;
      });
}

void MagicKeyboardEngine::startSocketServer() {
  std::string path = ipc::getSocketPath();

  // Remove stale socket file (ignore errors - file may not exist)
  unlink(path.c_str());

  serverFd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (serverFd_ < 0) {
    MKLOG(Error) << "socket() failed: " << strerror(errno);
    return;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

  if (bind(serverFd_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    MKLOG(Error) << "bind() failed: " << strerror(errno);
    close(serverFd_);
    serverFd_ = -1;
    return;
  }

  if (listen(serverFd_, 1) < 0) {
    MKLOG(Error) << "listen() failed: " << strerror(errno);
    close(serverFd_);
    serverFd_ = -1;
    return;
  }

  MKLOG(Info) << "Socket server listening: " << path;

  serverEvent_ = instance_->eventLoop().addIOEvent(
      serverFd_, fcitx::IOEventFlag::In,
      [this](fcitx::EventSource *, int fd, fcitx::IOEventFlags) {
        if (shuttingDown_)
          return true;

        int clientFd =
            accept4(fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (clientFd >= 0) {
          MKLOG(Info) << "Client connected (fd=" << clientFd << ")";
          uiSpawnPending_ = false;

          auto client = std::make_unique<Client>();
          client->event = instance_->eventLoop().addIOEvent(
              clientFd, fcitx::IOEventFlag::In,
              [this, clientFd](fcitx::EventSource *, int, fcitx::IOEventFlags) {
                if (shuttingDown_)
                  return true;

                char buf[1024];
                ssize_t n = read(clientFd, buf, sizeof(buf) - 1);
                if (n > 0) {
                  buf[n] = '\0';
                  auto &it = clients_[clientFd];
                  it->buffer += buf;

                  size_t pos;
                  while ((pos = it->buffer.find('\n')) != std::string::npos) {
                    std::string line = it->buffer.substr(0, pos);
                    it->buffer.erase(0, pos + 1);
                    if (!line.empty())
                      processLine(line, clientFd);
                  }
                } else if (n == 0) {
                  auto it = clients_.find(clientFd);
                  if (it != clients_.end()) {
                    if (it->second->role == "ui" || it->second->role.empty()) {
                      MKLOG(Info) << "UI disconnected (fd=" << clientFd << ")";
                    }
                    clients_.erase(it);
                  }
                }
                return true;
              });

          clients_[clientFd] = std::move(client);

          // Sync visibility state to newly-connected client
          if (visibilityState_ == VisibilityState::Visible) {
            std::string msg = "{\"type\":\"show\"}\n";
            write(clientFd, msg.c_str(), msg.size());
          }
        }
        return true;
      });
}

void MagicKeyboardEngine::stopSocketServer() {
  // HARDENED ORDER: kill event sources first to stop callbacks
  for (auto const &[fd, client] : clients_) {
    client->event.reset();
    close(fd);
  }
  clients_.clear();
  serverEvent_.reset();

  // Only then close the fds
  if (serverFd_ >= 0) {
    close(serverFd_);
    unlink(ipc::getSocketPath().c_str());
    serverFd_ = -1;
  }
}

void MagicKeyboardEngine::launchUI() {
  uiSpawnPending_ = true;

  pid_t pid = fork();
  if (pid == 0) {
    setsid();

    const char *paths[] = {"/usr/local/bin/magickeyboard-ui",
                           "/usr/bin/magickeyboard-ui", nullptr};

    for (const char **p = paths; *p; ++p) {
      execl(*p, "magickeyboard-ui", nullptr);
    }

    _exit(127);
  } else if (pid > 0) {
    uiPid_ = pid;
    MKLOG(Info) << "UI spawned, pid=" << pid;
  } else {
    MKLOG(Error) << "fork() failed: " << strerror(errno);
    uiSpawnPending_ = false;
  }
}

void MagicKeyboardEngine::loadDictionary() {
  dictionary_.clear();
  for (int i = 0; i < 26; ++i)
    for (int j = 0; j < 26; ++j)
      buckets_[i][j].clear();

  std::vector<std::string> searchPaths = {
      "data/dict/", "/usr/local/share/magic-keyboard/dict/",
      "/usr/share/magic-keyboard/dict/"};

  std::string wordPath, freqPath;
  for (const auto &p : searchPaths) {
    if (std::ifstream(p + "words.txt").is_open()) {
      wordPath = p + "words.txt";
      freqPath = p + "freq.tsv";
      break;
    }
  }

  if (wordPath.empty()) {
    MKLOG(Error) << "Dictionary not found";
    return;
  }

  MKLOG(Info) << "Loading dictionary from: " << wordPath;

  // Load frequencies first
  std::unordered_map<std::string, uint32_t> freqs;
  std::ifstream ff(freqPath);
  std::string line;
  while (std::getline(ff, line)) {
    size_t tab = line.find('\t');
    if (tab != std::string::npos) {
      freqs[line.substr(0, tab)] = std::stoul(line.substr(tab + 1));
    }
  }

  // Load words
  std::ifstream wf(wordPath);
  while (std::getline(wf, line)) {
    if (line.empty())
      continue;
    DictWord dw;
    dw.word = line;
    dw.freq = freqs.count(line) ? freqs[line] : 0;
    dw.len = (int)line.length();
    dw.first = (char)std::tolower(line[0]);
    dw.last = (char)std::tolower(line.back());
    dictionary_.push_back(dw);

    int fidx = dw.first - 'a';
    int lidx = dw.last - 'a';
    if (fidx >= 0 && fidx < 26 && lidx >= 0 && lidx < 26) {
      buckets_[fidx][lidx].push_back((int)dictionary_.size() - 1);
    }
  }
  MKLOG(Info) << "Loaded " << dictionary_.size() << " words";
}

std::vector<int> MagicKeyboardEngine::getShortlist(const std::string &keys) {
  if (keys.empty())
    return {};
  int fidx = std::tolower(keys[0]) - 'a';
  int lidx = std::tolower(keys.back()) - 'a';
  if (fidx < 0 || fidx >= 26 || lidx < 0 || lidx >= 26)
    return {};

  std::vector<int> result;
  int targetLen = (int)keys.length();
  for (int idx : buckets_[fidx][lidx]) {
    const auto &dw = dictionary_[idx];
    if (std::abs(dw.len - targetLen) <= 3) {
      result.push_back(idx);
    }
  }
  return result;
}

std::vector<MagicKeyboardEngine::Candidate>
MagicKeyboardEngine::generateCandidates(const std::string &keys,
                                        size_t pointsCount) {
  auto start = std::chrono::steady_clock::now();
  auto shortlist = getShortlist(keys);
  std::vector<Candidate> candidates;

  for (int idx : shortlist) {
    const auto &dw = dictionary_[idx];
    candidates.push_back({dw.word, scoreCandidate(keys, dw)});
  }

  std::sort(
      candidates.begin(), candidates.end(),
      [](const Candidate &a, const Candidate &b) { return a.score > b.score; });

  if (candidates.size() > 8)
    candidates.resize(8);

  currentCandidates_ = candidates;
  candidateMode_ = !candidates.empty();

  auto end = std::chrono::steady_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
                .count();

  MKLOG(Info) << "SwipeCand layout=qwerty points=" << pointsCount
              << " keys=" << keys.length() << " shortlist=" << shortlist.size()
              << " cand=" << candidates.size()
              << " top=" << (candidates.empty() ? "?" : candidates[0].word)
              << " gen=" << ms << "ms"
              << " dict=" << dictionary_.size();

  return candidates;
}

int MagicKeyboardEngine::levenshtein(const std::string &s1,
                                     const std::string &s2, int limit) {
  int n = s1.length();
  int m = s2.length();
  if (std::abs(n - m) > limit)
    return limit + 1;

  std::vector<int> prev(m + 1);
  std::vector<int> curr(m + 1);

  for (int j = 0; j <= m; ++j)
    prev[j] = j;

  for (int i = 1; i <= n; ++i) {
    curr[0] = i;
    int minRow = curr[0];
    for (int j = 1; j <= m; ++j) {
      int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
      curr[j] = std::min({curr[j - 1] + 1, prev[j] + 1, prev[j - 1] + cost});
      minRow = std::min(minRow, curr[j]);
    }
    if (minRow > limit)
      return limit + 1;
    prev = curr;
  }
  return prev[m];
}

double MagicKeyboardEngine::scoreCandidate(const std::string &keys,
                                           const DictWord &dw) {
  // 1. Edit distance (capped at 7)
  int dist = levenshtein(keys, dw.word, 7);

  // 2. Bigram overlap
  // Use a small fixed array for matches (uint16_t: a*26 + b)
  auto getBigrams = [](const std::string &s) {
    std::vector<uint16_t> b;
    for (size_t i = 0; i + 1 < s.length(); ++i) {
      if (std::isalpha(s[i]) && std::isalpha(s[i + 1])) {
        b.push_back((std::tolower(s[i]) - 'a') * 26 +
                    (std::tolower(s[i + 1]) - 'a'));
      }
    }
    return b;
  };

  auto b1 = getBigrams(keys);
  auto b2 = getBigrams(dw.word);
  int overlaps = 0;
  for (auto bg1 : b1) {
    for (auto bg2 : b2) {
      if (bg1 == bg2) {
        overlaps++;
        break;
      }
    }
  }

  // 3. Frequency component
  // Using log(freq) for scaling. Adding 1 to avoid log(0).
  double freqScore = std::log1p(dw.freq);

  // Final formula: score = -2.2*edit + 1.0*bigrams + 0.8*freqScore
  return -2.2 * dist + 1.0 * overlaps + 0.8 * freqScore;
}

} // namespace magickeyboard

FCITX_ADDON_FACTORY(magickeyboard::MagicKeyboardFactory);
