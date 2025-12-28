/**
 * Magic Keyboard - Fcitx5 Input Method Engine
 * v0.1: Focus-driven show/hide via InputContext FocusIn/FocusOut
 */

#include "magickeyboard.h"
#include "protocol.h"

#include <fcitx-utils/event.h>
#include <fcitx-utils/log.h>
#include <fcitx/event.h>
#include <fcitx/inputmethodmanager.h>
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

  // Watch for actual text field focus changes
  // These fire when ANY InputContext gains/loses focus
  focusInConn_ = instance_->watchEvent(
      fcitx::EventType::InputContextFocusIn, fcitx::EventWatcherPhase::Default,
      [this](fcitx::Event &event) {
        // Extract IC info immediately, don't store pointer
        auto &icEvent = static_cast<fcitx::FocusInEvent &>(event);
        auto *ic = icEvent.inputContext();
        if (!ic)
          return;

        handleFocusIn(ic);
      });

  focusOutConn_ = instance_->watchEvent(
      fcitx::EventType::InputContextFocusOut, fcitx::EventWatcherPhase::Default,
      [this](fcitx::Event &event) {
        auto &icEvent = static_cast<fcitx::FocusOutEvent &>(event);
        auto *ic = icEvent.inputContext();
        if (!ic)
          return;

        handleFocusOut(ic);
      });

  MKLOG(Info) << "Magic Keyboard engine ready";
}

MagicKeyboardEngine::~MagicKeyboardEngine() {
  MKLOG(Info) << "Magic Keyboard engine shutting down";

  // Release watchers first
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
  // Track current IC for key commits (valid during this callback)
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
  if (currentIC_) {
    currentIC_->inputPanel().reset();
    currentIC_->updatePreedit();
  }
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

  reason = "ok";
  return 1;
}

void MagicKeyboardEngine::handleFocusIn(fcitx::InputContext *ic) {
  std::string program = ic ? ic->program() : "?";
  std::string reason;
  int show = shouldShowKeyboard(ic, reason);

  // Single-line operator log
  MKLOG(Info) << "FocusIn: " << program << " show=" << show << " (" << reason
              << ")";

  if (show) {
    currentIC_ = ic;
    ensureUIRunning();
    showKeyboard();
  } else if (keyboardVisible_) {
    hideKeyboard();
  }
}

void MagicKeyboardEngine::handleFocusOut(fcitx::InputContext *ic) {
  std::string program = ic ? ic->program() : "?";

  if (keyboardVisible_) {
    MKLOG(Info) << "FocusOut: " << program << " -> hide";
    hideKeyboard();
  }

  if (currentIC_ == ic) {
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
      return;
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
