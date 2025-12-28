#pragma once

#include <fcitx-utils/event.h>
#include <fcitx-utils/handlertable.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace magickeyboard {

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
  void handleFocusIn(fcitx::InputContext *ic);
  void handleFocusOut(fcitx::InputContext *ic);
  int shouldShowKeyboard(fcitx::InputContext *ic, std::string &reason);

  void showKeyboard();
  void hideKeyboard();
  void sendToUI(const std::string &msg);
  void handleKeyPress(const std::string &key);
  void processLine(const std::string &line);
  void startSocketServer();
  void stopSocketServer();
  void launchUI();
  void ensureUIRunning();

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

  // Socket event sources
  std::unique_ptr<fcitx::EventSource> serverEvent_;
  std::unique_ptr<fcitx::EventSource> clientEvent_;

  int serverFd_ = -1;
  int clientFd_ = -1;
  std::string readBuffer_;

  pid_t uiPid_ = 0;
  bool uiSpawnPending_ = false;

  // Pointer valid only during callback context
  fcitx::InputContext *currentIC_ = nullptr;

  bool keyboardVisible_ = false;
};

class MagicKeyboardFactory : public fcitx::AddonFactory {
public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override;
};

} // namespace magickeyboard
