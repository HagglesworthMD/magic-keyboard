#pragma once

/**
 * Magic Keyboard - Fcitx5 Input Method Engine
 * 
 * This addon:
 * - Tracks InputContext lifecycle (focus in/out)
 * - Maintains composition state (preedit, candidates)
 * - Commits text to focused application
 * - Signals UI process to show/hide keyboard
 * - Handles shortcut actions (copy/paste/cut/selectall)
 */

#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include <memory>
#include <string>

namespace magickeyboard {

class MagicKeyboardEngine;

/**
 * Factory class to create engine instances
 */
class MagicKeyboardFactory : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance* create(fcitx::AddonManager* manager) override;
};

/**
 * Main Input Method Engine implementation
 */
class MagicKeyboardEngine : public fcitx::InputMethodEngineV2 {
public:
    explicit MagicKeyboardEngine(fcitx::Instance* instance);
    ~MagicKeyboardEngine() override;

    // AddonInstance interface
    void reloadConfig() override;
    
    // InputMethodEngine interface
    void activate(const fcitx::InputMethodEntry& entry,
                  fcitx::InputContextEvent& event) override;
    void deactivate(const fcitx::InputMethodEntry& entry,
                    fcitx::InputContextEvent& event) override;
    void keyEvent(const fcitx::InputMethodEntry& entry,
                  fcitx::KeyEvent& keyEvent) override;
    void reset(const fcitx::InputMethodEntry& entry,
               fcitx::InputContextEvent& event) override;
    
    // Get instance for addon lookups
    fcitx::Instance* instance() { return instance_; }

private:
    // Show/hide UI
    void showKeyboard();
    void hideKeyboard();
    
    // Handle text input from UI
    void handleKeyPress(const std::string& key, 
                        const std::vector<std::string>& modifiers);
    void handleAction(const std::string& action);
    
    // IPC helpers
    void startSocketServer();
    void stopSocketServer();
    void launchUI();

    fcitx::Instance* instance_;
    fcitx::InputContext* currentIC_ = nullptr;
    
    // Socket server state (placeholder for v0.1)
    int serverFd_ = -1;
    
    // UI process tracking
    pid_t uiPid_ = 0;
};

} // namespace magickeyboard
