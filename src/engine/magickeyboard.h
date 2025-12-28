#pragma once

#include <fcitx-utils/event.h>
#include <fcitx-utils/handlertable.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

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
  // Focus event handlers - IC pointer valid only during call
  void handleFocusIn(fcitx::InputContext *ic);
  void handleFocusOut(fcitx::InputContext *ic);

  // IM and capability checks
  bool isMagicKeyboardActive(fcitx::InputContext *ic);
  bool shouldShowKeyboard(fcitx::InputContext *ic);

  void showKeyboard();
  void hideKeyboard();
  void sendToUI(const std::string &msg);
  void handleKeyPress(const std::string &key);
  void processLine(const std::string &line);
  void startSocketServer();
  void stopSocketServer();
  void launchUI();
  void ensureUIRunning();

  fcitx::Instance *instance_;

  // Current IC for commits - updated on focus/activate
  fcitx::InputContext *currentIC_ = nullptr;

  // Socket IPC
  int serverFd_ = -1;
  int clientFd_ = -1;
  std::unique_ptr<fcitx::EventSource> serverEvent_;
  std::unique_ptr<fcitx::EventSource> clientEvent_;
  std::string readBuffer_;

  // UI process
  pid_t uiPid_ = 0;
  bool uiSpawnPending_ = false;

  // Event watcher connections - must outlive callbacks
  std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> focusInConn_;
  std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> focusOutConn_;

  // State
  bool keyboardVisible_ = false;
};

class MagicKeyboardFactory : public fcitx::AddonFactory {
public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override;
};

} // namespace magickeyboard
