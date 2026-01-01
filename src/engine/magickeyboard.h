#pragma once

#include <fcitx-utils/event.h>
#include <fcitx-utils/handlertable.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

#include "lexicon/Trie.h"
#include "settings.h"
#include "shark2.h"
#include "user_data.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace magickeyboard {

// Focus & Visibility Control State Machine (Agent #2)
// Prevents flicker via debounced show/hide transitions
enum class VisibilityState {
  Hidden,      // Keyboard not visible
  PendingShow, // FocusIn received, waiting debounce before showing
  Visible,     // Keyboard visible and active
  PendingHide  // FocusOut received, waiting debounce before hiding
};

class MagicKeyboardEngine : public fcitx::InputMethodEngineV2 {
public:
  explicit MagicKeyboardEngine(fcitx::Instance *instance);
  ~MagicKeyboardEngine() override;

  std::vector<fcitx::InputMethodEntry> listInputMethods() override;

  void reloadConfig() override;
  void activate(const fcitx::InputMethodEntry &,
                fcitx::InputContextEvent &) override;
  void deactivate(const fcitx::InputMethodEntry &,
                  fcitx::InputContextEvent &) override;
  void keyEvent(const fcitx::InputMethodEntry &, fcitx::KeyEvent &) override;
  void reset(const fcitx::InputMethodEntry &,
             fcitx::InputContextEvent &) override;

private:
  // === Focus & Visibility Control (Agent #2) ===
  void handleFocusIn(fcitx::InputContext *ic);
  void handleFocusOut(fcitx::InputContext *ic);
  int shouldShowKeyboard(fcitx::InputContext *ic, std::string &reason);

  // Debounced state machine methods
  void scheduleDebounce(VisibilityState target, int delayMs);
  void cancelDebounce();
  void executeTransition(VisibilityState target);
  void executeShow();
  void executeHide();

  void sendToUI(const std::string &msg);
  void handleKeyPress(const std::string &key);
  void processLine(const std::string &line, int clientFd);
  void startSocketServer();
  void stopSocketServer();
  void launchUI();
  void ensureUIRunning();

  // Pick best IC for key injection (current vs cached fallback)
  fcitx::InputContext *pickTargetInputContext();

  // === Clipboard & Shortcut Agent (Agent #7) ===
  void handleShortcutAction(const std::string &action);
  bool isTerminal(const std::string &program);

  // === Settings & Learning ===
  void handleSettingsRequest(int clientFd);
  void handleSettingUpdate(const std::string &key, const std::string &value);
  void sendSettingsToUI();
  void recordWordCommit(const std::string &word);

  // === Snap-to-caret positioning ===
  void sendCaretPosition(fcitx::InputContext *ic);

  // Watchdog to ensure keyboard hides if focus tracking fails
  void startWatchdog();

  // === MEMBER DECLARATION ORDER MATTERS FOR DESTRUCTION ===
  // Reverse order: first connections/watchers die, then sockets, then
  // primitives.

  fcitx::Instance *instance_;

  // Shutdown gate - checked in all callbacks
  std::atomic<bool> shuttingDown_{false};

  // Event watchers - MUST be destroyed before the members they access
  std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> focusInConn_;
  std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> focusOutConn_;

  // Timer for watchdog
  std::unique_ptr<fcitx::EventSource> watchdogTimer_;

  // === Focus & Visibility State Machine (Agent #2) ===
  // Debounce configuration (milliseconds) - tuned for responsiveness vs flicker
  static constexpr int DEBOUNCE_SHOW_MS = 50; // Wait before showing (fast)
  static constexpr int DEBOUNCE_HIDE_MS =
      100; // Wait before hiding (prevents flicker)
  static constexpr int WATCHDOG_MS = 500; // Safety check interval

  VisibilityState visibilityState_ = VisibilityState::Hidden;
  fcitx::InputContext *pendingIC_ =
      nullptr; // IC that triggered pending transition
  std::unique_ptr<fcitx::EventSource> debounceTimer_;

  // Socket event sources
  std::unique_ptr<fcitx::EventSource> serverEvent_;
  struct Client {
    std::unique_ptr<fcitx::EventSource> event;
    std::string buffer;
    std::string role;
  };
  std::unordered_map<int, std::unique_ptr<Client>> clients_;

  int serverFd_ = -1;

  pid_t uiPid_ = 0;
  bool uiSpawnPending_ = false;

  // Pointer valid only during callback context
  fcitx::InputContext *currentIC_ = nullptr;
  // Fallback cache for when currentIC_ is null (lost focus)
  fcitx::InputContext *lastFocusedIc_ = nullptr;
  // CRITICAL: Preserved IC that survives UI focus events (emergency fix)
  // When UI is visible, this holds the target IC for key injection
  fcitx::InputContext *preservedIC_ = nullptr;

  // v0.2.2 Geometry model
  struct Point {
    double x, y;
  };
  struct Rect {
    double x, y, w, h;
    bool contains(const Point &p) const {
      return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
    }
  };
  struct Key {
    std::string id;
    Rect r;
    Point center;
  };
  std::vector<Key> keys_;

  // v0.2.3 Dictionary engine
  struct DictWord {
    std::string word;
    uint32_t freq;
    char first, last;
    int len;
  };
  struct Candidate {
    std::string word;
    double score;
  };
  std::vector<DictWord> dictionary_;
  std::unique_ptr<lexicon::Trie> trie_;
  std::vector<int> buckets_[26][26];

  std::vector<Candidate> currentCandidates_;
  bool candidateMode_ = false;
  std::chrono::steady_clock::time_point lastToggleTime_;

  // SHARK2 engine for gesture recognition
  shark2::Shark2Engine shark2Engine_;
  bool useShark2_ = true; // Enable SHARK2 algorithm

  // Learning context
  std::string lastCommittedWord_;

  void loadLayout(const std::string &layoutName);
  void loadDictionary();
  std::vector<std::string> mapPathToSequence(const std::vector<Point> &path);
  std::vector<int> getShortlist(const std::string &keys);
  std::vector<Candidate> generateCandidates(const std::string &keys,
                                            size_t pointsCount);

  int levenshtein(const std::string &s1, const std::string &s2, int limit);
  double scoreCandidate(const std::string &keys, const DictWord &dw);

  std::vector<std::string> dataDirs() const;
  std::string findDataFile(const std::string &relPath) const;
};

class MagicKeyboardFactory : public fcitx::AddonFactory {
public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override;
};

} // namespace magickeyboard
