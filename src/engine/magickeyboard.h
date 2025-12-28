#pragma once

#include <fcitx-utils/event.h>
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

  // List our input method
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
  void showKeyboard();
  void hideKeyboard();
  void sendToUI(const std::string &msg);
  void handleKeyPress(const std::string &key);
  void processLine(const std::string &line);
  void startSocketServer();
  void stopSocketServer();
  void launchUI();

  fcitx::Instance *instance_;
  fcitx::InputContext *currentIC_ = nullptr;

  int serverFd_ = -1;
  int clientFd_ = -1;
  std::unique_ptr<fcitx::EventSource> serverEvent_;
  std::unique_ptr<fcitx::EventSource> clientEvent_;
  std::string readBuffer_;

  pid_t uiPid_ = 0;
};

class MagicKeyboardFactory : public fcitx::AddonFactory {
public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override;
};

} // namespace magickeyboard
