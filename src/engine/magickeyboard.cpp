/**
 * Magic Keyboard - Fcitx5 Input Method Engine
 * v0.1: Focus-driven show/hide + click-to-commit via Unix socket
 */

#include "magickeyboard.h"
#include "protocol.h"

#include <fcitx-utils/event.h>
#include <fcitx-utils/log.h>
#include <fcitx/event.h>
#include <fcitx/inputpanel.h>

#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
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
  MKLOG(Info) << "Magic Keyboard engine starting";

  startSocketServer();

  // Watch for our IM being activated/deactivated on any IC
  // These events tell us when OUR input method is selected
  focusInConn_ = instance_->watchEvent(
      fcitx::EventType::InputContextInputMethodActivated,
      fcitx::EventWatcherPhase::Default, [this](fcitx::Event &event) {
        auto &imEvent = static_cast<fcitx::InputMethodActivatedEvent &>(event);
        if (imEvent.name() == "magickeyboard") {
          onFocusIn(imEvent.inputContext());
        }
      });

  focusOutConn_ = instance_->watchEvent(
      fcitx::EventType::InputContextInputMethodDeactivated,
      fcitx::EventWatcherPhase::Default, [this](fcitx::Event &event) {
        auto &imEvent =
            static_cast<fcitx::InputMethodDeactivatedEvent &>(event);
        if (imEvent.name() == "magickeyboard") {
          onFocusOut(imEvent.inputContext());
        }
      });

  MKLOG(Info) << "Magic Keyboard engine ready";
}

MagicKeyboardEngine::~MagicKeyboardEngine() {
  MKLOG(Info) << "Magic Keyboard engine shutting down";

  focusInConn_.reset();
  focusOutConn_.reset();
  stopSocketServer();

  if (uiPid_ > 0) {
    kill(uiPid_, SIGTERM);
    waitpid(uiPid_, nullptr, WNOHANG);
  }
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
  MKLOG(Debug) << "activate(): " << currentIC_->program();
  // Show will happen via InputMethodActivated event
}

void MagicKeyboardEngine::deactivate(const fcitx::InputMethodEntry &,
                                     fcitx::InputContextEvent &) {
  MKLOG(Debug) << "deactivate()";
  currentIC_ = nullptr;
  // Hide will happen via InputMethodDeactivated event
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

void MagicKeyboardEngine::onFocusIn(fcitx::InputContext *ic) {
  if (!ic)
    return;

  currentIC_ = ic;
  MKLOG(Info) << "Focus-in -> show (" << ic->program() << ")";

  ensureUIRunning();
  showKeyboard();
}

void MagicKeyboardEngine::onFocusOut(fcitx::InputContext *ic) {
  if (!ic)
    return;

  MKLOG(Info) << "Focus-out -> hide";
  hideKeyboard();

  if (ic == currentIC_) {
    currentIC_ = nullptr;
  }
}

void MagicKeyboardEngine::showKeyboard() {
  if (keyboardVisible_)
    return;

  keyboardVisible_ = true;
  sendToUI("{\"type\":\"show\"}\n");
}

void MagicKeyboardEngine::hideKeyboard() {
  if (!keyboardVisible_)
    return;

  keyboardVisible_ = false;
  sendToUI("{\"type\":\"hide\"}\n");
}

void MagicKeyboardEngine::sendToUI(const std::string &msg) {
  if (clientFd_ < 0) {
    MKLOG(Debug) << "No UI connected";
    return;
  }

  ssize_t n = write(clientFd_, msg.c_str(), msg.size());
  if (n < 0) {
    MKLOG(Warn) << "Send failed: " << strerror(errno);
  }
}

void MagicKeyboardEngine::ensureUIRunning() {
  if (clientFd_ >= 0)
    return;
  if (uiSpawnPending_)
    return;

  if (uiPid_ > 0) {
    int status;
    pid_t result = waitpid(uiPid_, &status, WNOHANG);
    if (result == 0)
      return; // Still running
    uiPid_ = 0;
  }

  launchUI();
}

void MagicKeyboardEngine::handleKeyPress(const std::string &key) {
  if (!currentIC_) {
    MKLOG(Warn) << "Key but no active IC";
    return;
  }

  MKLOG(Debug) << "Commit: " << key;

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

void MagicKeyboardEngine::processLine(const std::string &line) {
  if (line.find("\"type\":\"key\"") != std::string::npos) {
    auto pos = line.find("\"text\":\"");
    if (pos != std::string::npos) {
      pos += 8;
      auto end = line.find("\"", pos);
      if (end != std::string::npos) {
        handleKeyPress(line.substr(pos, end - pos));
      }
    }
  }
}

void MagicKeyboardEngine::startSocketServer() {
  std::string path = ipc::getSocketPath();

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

  MKLOG(Info) << "Socket: " << path;

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
          uiSpawnPending_ = false;

          clientEvent_ = instance_->eventLoop().addIOEvent(
              clientFd_, fcitx::IOEventFlag::In,
              [this](fcitx::EventSource *, int, fcitx::IOEventFlags) {
                char buf[4096];
                ssize_t n = read(clientFd_, buf, sizeof(buf) - 1);
                if (n > 0) {
                  buf[n] = '\0';
                  readBuffer_ += buf;

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

          // If keyboard should be visible, tell UI immediately
          if (keyboardVisible_) {
            sendToUI("{\"type\":\"show\"}\n");
          }
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

} // namespace magickeyboard

FCITX_ADDON_FACTORY(magickeyboard::MagicKeyboardFactory);
