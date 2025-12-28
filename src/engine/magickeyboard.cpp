/**
 * Magic Keyboard - Fcitx5 Input Method Engine
 * v0.1: Socket IPC for show/hide and key commits
 */

#include "magickeyboard.h"
#include "protocol.h"

#include <fcitx-utils/event.h>
#include <fcitx-utils/log.h>
#include <fcitx/inputpanel.h>

#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace magickeyboard {

FCITX_DEFINE_LOG_CATEGORY(magickeyboard_log, "magickeyboard");
#define MKLOG(level) FCITX_LOGC(magickeyboard_log, level)

fcitx::AddonInstance *
MagicKeyboardFactory::create(fcitx::AddonManager *manager) {
  return new MagicKeyboardEngine(manager->instance());
}

MagicKeyboardEngine::MagicKeyboardEngine(fcitx::Instance *instance)
    : instance_(instance) {
  MKLOG(Info) << "Magic Keyboard engine initializing";
  startSocketServer();
}

MagicKeyboardEngine::~MagicKeyboardEngine() {
  MKLOG(Info) << "Magic Keyboard engine shutting down";
  stopSocketServer();
  if (uiPid_ > 0) {
    kill(uiPid_, SIGTERM);
  }
}

void MagicKeyboardEngine::reloadConfig() {}

std::vector<fcitx::InputMethodEntry> MagicKeyboardEngine::listInputMethods() {
  std::vector<fcitx::InputMethodEntry> result;

  // Create entry with: uniqueName, name, languageCode, addonName
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
  MKLOG(Info) << "Activated: " << currentIC_->program();

  if (uiPid_ <= 0)
    launchUI();
  showKeyboard();
}

void MagicKeyboardEngine::deactivate(const fcitx::InputMethodEntry &,
                                     fcitx::InputContextEvent &) {
  MKLOG(Info) << "Deactivated";
  hideKeyboard();
  currentIC_ = nullptr;
}

void MagicKeyboardEngine::keyEvent(const fcitx::InputMethodEntry &,
                                   fcitx::KeyEvent &keyEvent) {
  // Pass through physical keyboard events
  keyEvent.filterAndAccept();
}

void MagicKeyboardEngine::reset(const fcitx::InputMethodEntry &,
                                fcitx::InputContextEvent &) {
  if (currentIC_) {
    currentIC_->inputPanel().reset();
    currentIC_->updatePreedit();
  }
}

void MagicKeyboardEngine::showKeyboard() { sendToUI("{\"type\":\"show\"}\n"); }

void MagicKeyboardEngine::hideKeyboard() { sendToUI("{\"type\":\"hide\"}\n"); }

void MagicKeyboardEngine::sendToUI(const std::string &msg) {
  if (clientFd_ >= 0) {
    ssize_t n = write(clientFd_, msg.c_str(), msg.size());
    if (n < 0) {
      MKLOG(Warn) << "Failed to send to UI: " << strerror(errno);
    }
  }
}

void MagicKeyboardEngine::handleKeyPress(const std::string &key) {
  if (!currentIC_) {
    MKLOG(Warn) << "Key but no IC";
    return;
  }

  MKLOG(Debug) << "Key: " << key;

  if (key == "backspace") {
    currentIC_->forwardKey(fcitx::Key(FcitxKey_BackSpace), false);
    currentIC_->forwardKey(fcitx::Key(FcitxKey_BackSpace), true);
  } else if (key == "enter") {
    currentIC_->forwardKey(fcitx::Key(FcitxKey_Return), false);
    currentIC_->forwardKey(fcitx::Key(FcitxKey_Return), true);
  } else if (key == "space") {
    currentIC_->commitString(" ");
  } else if (key.length() == 1) {
    currentIC_->commitString(key);
  } else {
    // Multi-char like uppercase
    currentIC_->commitString(key);
  }
}

void MagicKeyboardEngine::processLine(const std::string &line) {
  // Simple JSON parsing for v0.1
  // Format: {"type":"key","text":"a"}

  if (line.find("\"type\":\"key\"") != std::string::npos) {
    auto pos = line.find("\"text\":\"");
    if (pos != std::string::npos) {
      pos += 8;
      auto end = line.find("\"", pos);
      if (end != std::string::npos) {
        std::string text = line.substr(pos, end - pos);
        handleKeyPress(text);
      }
    }
  }
}

void MagicKeyboardEngine::startSocketServer() {
  std::string path = ipc::getSocketPath();

  // Remove stale socket
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

  MKLOG(Info) << "Socket server listening at: " << path;

  // Register with Fcitx5 event loop
  serverEvent_ = instance_->eventLoop().addIOEvent(
      serverFd_, fcitx::IOEventFlag::In,
      [this](fcitx::EventSource *, int fd, fcitx::IOEventFlags) {
        int client =
            accept4(fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client >= 0) {
          MKLOG(Info) << "UI connected";
          if (clientFd_ >= 0)
            close(clientFd_);
          clientFd_ = client;

          // Set up read handler for client
          clientEvent_ = instance_->eventLoop().addIOEvent(
              clientFd_, fcitx::IOEventFlag::In,
              [this](fcitx::EventSource *, int, fcitx::IOEventFlags) {
                char buf[4096];
                ssize_t n = read(clientFd_, buf, sizeof(buf) - 1);
                if (n > 0) {
                  buf[n] = '\0';
                  readBuffer_ += buf;

                  // Process complete lines
                  size_t pos;
                  while ((pos = readBuffer_.find('\n')) != std::string::npos) {
                    std::string line = readBuffer_.substr(0, pos);
                    readBuffer_.erase(0, pos + 1);
                    if (!line.empty())
                      processLine(line);
                  }
                } else if (n == 0) {
                  MKLOG(Info) << "UI disconnected";
                  close(clientFd_);
                  clientFd_ = -1;
                  clientEvent_.reset();
                }
                return true;
              });
        }
        return true;
      });
}

void MagicKeyboardEngine::stopSocketServer() {
  clientEvent_.reset();
  serverEvent_.reset();
  if (clientFd_ >= 0) {
    close(clientFd_);
    clientFd_ = -1;
  }
  if (serverFd_ >= 0) {
    close(serverFd_);
    unlink(ipc::getSocketPath().c_str());
    serverFd_ = -1;
  }
}

void MagicKeyboardEngine::launchUI() {
  MKLOG(Info) << "Launching UI";

  pid_t pid = fork();
  if (pid == 0) {
    // Child - find UI binary
    // Try local build first, then installed
    const char *paths[] = {"./build/bin/magickeyboard-ui",
                           "/usr/local/bin/magickeyboard-ui",
                           "/usr/bin/magickeyboard-ui", nullptr};
    for (const char **p = paths; *p; ++p) {
      execl(*p, "magickeyboard-ui", nullptr);
    }
    _exit(1);
  } else if (pid > 0) {
    uiPid_ = pid;
    MKLOG(Info) << "UI launched, pid=" << pid;
  } else {
    MKLOG(Error) << "fork() failed";
  }
}

} // namespace magickeyboard

FCITX_ADDON_FACTORY(magickeyboard::MagicKeyboardFactory);
