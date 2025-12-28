#pragma once

#include <fcitx-utils/event.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/focusgroup.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
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

  // Called by focus watcher
  void onFocusIn(fcitx::InputContext *ic);
  void onFocusOut(fcitx::InputContext *ic);

private:
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

  // Focus tracking connections
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
