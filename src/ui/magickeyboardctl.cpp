#include "protocol.h"
#include <cstring>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: magickeyboardctl [show|hide|toggle]" << std::endl;
    return 1;
  }

  std::string cmd = argv[1];
  std::string msg;
  if (cmd == "show") {
    msg = "{\"type\":\"ui_show\"}\n";
  } else if (cmd == "hide") {
    msg = "{\"type\":\"ui_hide\"}\n";
  } else if (cmd == "toggle") {
    msg = "{\"type\":\"ui_toggle\"}\n";
  } else if (cmd == "ui-intent") {
    if (argc < 4) {
      std::cerr << "Usage: magickeyboardctl ui-intent <key|action|swipe> "
                   "<value> [...]"
                << std::endl;
      return 1;
    }
    std::string type = argv[2];
    std::string val = argv[3];
    if (type == "key") {
      msg = "{\"type\":\"ui_intent\",\"intent\":\"key\",\"value\":\"" + val +
            "\"}\n";
    } else if (type == "action") {
      msg = "{\"type\":\"ui_intent\",\"intent\":\"action\",\"value\":\"" + val +
            "\"}\n";
    } else if (type == "swipe") {
      // Swipe expects dir (val) and optional magnitude
      std::string mag = (argc > 4) ? argv[4] : "1.0";
      msg = "{\"type\":\"ui_intent\",\"intent\":\"swipe\",\"dir\":\"" + val +
            "\",\"mag\":" + mag + "}\n";
    } else {
      std::cerr << "Unknown intent type: " << type << std::endl;
      return 1;
    }
  } else {
    std::cerr << "Unknown command: " << cmd << std::endl;
    return 1;
  }

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return 1;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::string path = magickeyboard::ipc::getSocketPath();
  strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("connect");
    close(fd);
    return 1;
  }

  std::string hello = "{\"type\":\"hello\",\"role\":\"ctl\"}\n";
  std::string fullMsg = hello + msg;

  if (write(fd, fullMsg.c_str(), fullMsg.size()) < 0) {
    perror("write");
    close(fd);
    return 1;
  }

  // Wait for acknowledgment (Agent #4 fix) with 100ms timeout
  struct pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLIN;
  if (poll(&pfd, 1, 100) > 0) {
    char buf[128];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = '\0';
      // Optional: could print buf if we care about the message
    }
  }

  close(fd);
  return 0;
}
