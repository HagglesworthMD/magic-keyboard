/**
 * Magic Keyboard - Fcitx5 Input Method Engine Implementation
 * 
 * v0.1 Scaffold: Minimal implementation with logging.
 * - Tracks focus events
 * - Logs show/hide triggers
 * - Does not yet implement full IPC or text commit
 */

#include "magickeyboard.h"
#include "protocol.h"

#include <fcitx/inputpanel.h>
#include <fcitx-utils/log.h>

namespace magickeyboard {

// Logging category for this addon
FCITX_DEFINE_LOG_CATEGORY(magickeyboard_log, "magickeyboard");

#define MKLOG(level) FCITX_LOGC(magickeyboard_log, level)

// Factory implementation
fcitx::AddonInstance* MagicKeyboardFactory::create(fcitx::AddonManager* manager) {
    FCITX_UNUSED(manager);
    return new MagicKeyboardEngine(manager->instance());
}

// Engine implementation
MagicKeyboardEngine::MagicKeyboardEngine(fcitx::Instance* instance)
    : instance_(instance) {
    MKLOG(Info) << "Magic Keyboard engine initialized";
    
    // TODO: Start socket server for UI communication
    // startSocketServer();
}

MagicKeyboardEngine::~MagicKeyboardEngine() {
    MKLOG(Info) << "Magic Keyboard engine shutting down";
    stopSocketServer();
}

void MagicKeyboardEngine::reloadConfig() {
    MKLOG(Debug) << "Reloading configuration";
    // TODO: Load configuration from Fcitx5 config system
}

void MagicKeyboardEngine::activate(const fcitx::InputMethodEntry& entry,
                                    fcitx::InputContextEvent& event) {
    FCITX_UNUSED(entry);
    
    auto* ic = event.inputContext();
    currentIC_ = ic;
    
    MKLOG(Info) << "Input context activated: " << ic->program();
    
    // Signal UI to show keyboard
    showKeyboard();
}

void MagicKeyboardEngine::deactivate(const fcitx::InputMethodEntry& entry,
                                      fcitx::InputContextEvent& event) {
    FCITX_UNUSED(entry);
    
    auto* ic = event.inputContext();
    MKLOG(Info) << "Input context deactivated: " << ic->program();
    
    // Clear any pending composition
    if (currentIC_) {
        currentIC_->inputPanel().reset();
        currentIC_->updatePreedit();
        currentIC_->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
    }
    
    currentIC_ = nullptr;
    
    // Signal UI to hide keyboard
    hideKeyboard();
}

void MagicKeyboardEngine::keyEvent(const fcitx::InputMethodEntry& entry,
                                    fcitx::KeyEvent& keyEvent) {
    FCITX_UNUSED(entry);
    
    // For v0.1 scaffold: pass through all physical keyboard events
    // The on-screen keyboard will send events via socket, not through here
    
    MKLOG(Debug) << "Key event: " << keyEvent.key().toString();
    
    // Don't consume the event - let it pass to the application
    keyEvent.filterAndAccept();
}

void MagicKeyboardEngine::reset(const fcitx::InputMethodEntry& entry,
                                 fcitx::InputContextEvent& event) {
    FCITX_UNUSED(entry);
    FCITX_UNUSED(event);
    
    MKLOG(Debug) << "Reset called";
    
    // Clear composition state
    if (currentIC_) {
        currentIC_->inputPanel().reset();
        currentIC_->updatePreedit();
    }
}

void MagicKeyboardEngine::showKeyboard() {
    MKLOG(Info) << "Showing keyboard UI";
    
    // TODO v0.1: Send "show" message via socket
    // For now, just log
    
    // If UI not running, launch it
    if (uiPid_ == 0) {
        launchUI();
    }
    
    // Send show command
    // sendCommand(ipc::cmd_type::SHOW);
}

void MagicKeyboardEngine::hideKeyboard() {
    MKLOG(Info) << "Hiding keyboard UI";
    
    // TODO v0.1: Send "hide" message via socket
    // sendCommand(ipc::cmd_type::HIDE);
}

void MagicKeyboardEngine::handleKeyPress(const std::string& key,
                                          const std::vector<std::string>& modifiers) {
    FCITX_UNUSED(modifiers);
    
    if (!currentIC_) {
        MKLOG(Warn) << "Key press but no active input context";
        return;
    }
    
    MKLOG(Debug) << "Handling key press: " << key;
    
    // TODO v0.1: Handle special keys (backspace, enter, etc.)
    // For regular characters, commit directly
    if (key.length() == 1) {
        currentIC_->commitString(key);
    } else if (key == "space") {
        currentIC_->commitString(" ");
    } else if (key == "enter") {
        currentIC_->commitString("\n");
    } else if (key == "backspace") {
        // Forward backspace as a key event
        auto sym = fcitx::Key(FcitxKey_BackSpace);
        currentIC_->forwardKey(sym, false);
        currentIC_->forwardKey(sym, true);
    }
    // TODO: Handle shift state for uppercase
}

void MagicKeyboardEngine::handleAction(const std::string& action) {
    if (!currentIC_) {
        MKLOG(Warn) << "Action but no active input context";
        return;
    }
    
    MKLOG(Info) << "Handling action: " << action;
    
    fcitx::Key key;
    
    if (action == "copy") {
        key = fcitx::Key(FcitxKey_c, fcitx::KeyState::Ctrl);
    } else if (action == "paste") {
        key = fcitx::Key(FcitxKey_v, fcitx::KeyState::Ctrl);
    } else if (action == "cut") {
        key = fcitx::Key(FcitxKey_x, fcitx::KeyState::Ctrl);
    } else if (action == "selectall") {
        key = fcitx::Key(FcitxKey_a, fcitx::KeyState::Ctrl);
    } else {
        MKLOG(Warn) << "Unknown action: " << action;
        return;
    }
    
    currentIC_->forwardKey(key, false);  // Press
    currentIC_->forwardKey(key, true);   // Release
}

void MagicKeyboardEngine::startSocketServer() {
    std::string socketPath = ipc::getSocketPath();
    MKLOG(Info) << "Starting socket server at: " << socketPath;
    
    // TODO: Implement Unix domain socket server
    // - Create socket
    // - Bind to path
    // - Listen for connections
    // - Add to Fcitx5 event loop
}

void MagicKeyboardEngine::stopSocketServer() {
    if (serverFd_ >= 0) {
        MKLOG(Info) << "Stopping socket server";
        close(serverFd_);
        serverFd_ = -1;
        
        // Remove socket file
        std::string socketPath = ipc::getSocketPath();
        unlink(socketPath.c_str());
    }
}

void MagicKeyboardEngine::launchUI() {
    MKLOG(Info) << "Launching keyboard UI process";
    
    // TODO: Fork and exec magickeyboard-ui
    // For v0.1, we may run UI manually for testing
    
    // pid_t pid = fork();
    // if (pid == 0) {
    //     // Child
    //     execlp("magickeyboard-ui", "magickeyboard-ui", nullptr);
    //     _exit(1);
    // } else if (pid > 0) {
    //     uiPid_ = pid;
    // } else {
    //     MKLOG(Error) << "Failed to fork UI process";
    // }
}

} // namespace magickeyboard

// Register addon factory
FCITX_ADDON_FACTORY(magickeyboard::MagicKeyboardFactory);
