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
    if (type == "key" || type == "action") {
      if (argc < 4) {
        std::cerr << "Usage: magickeyboardctl ui-intent " << type << " <value>"
                  << std::endl;
        return 1;
      }
      std::string val = argv[3];
      msg = "{\"type\":\"ui_intent\",\"intent\":\"" + type + "\",\"value\":\"" +
            val + "\"}\n";
    } else if (type == "swipe") {
      if (argc < 4) {
        std::cerr << "Usage: magickeyboardctl ui-intent swipe <dir> [mag]"
                  << std::endl;
        return 1;
      }
      std::string val = argv[3];
      std::string mag = (argc > 4) ? argv[4] : "1.0";
      msg = "{\"type\":\"ui_intent\",\"intent\":\"swipe\",\"dir\":\"" + val +
            "\",\"mag\":" + mag + "}\n";
    } else if (type == "swipe-path") {
      if (argc < 4) {
        std::cerr << "Usage: magickeyboardctl ui-intent swipe-path <layout> "
                     "<x1,y1> <x2,y2> ..."
                  << std::endl;
        return 1;
      }
      std::string layout = argv[3];
      std::string pointsJson = "[";
      for (int i = 4; i < argc; ++i) {
        std::string ptStr = argv[i];
        size_t comma = ptStr.find(',');
        if (comma == std::string::npos) {
          std::cerr << "Malformed point: " << ptStr << " (expected x,y)"
                    << std::endl;
          return 1;
        }
        std::string x = ptStr.substr(0, comma);
        std::string y = ptStr.substr(comma + 1);
        pointsJson += "{\"x\":" + x + ",\"y\":" + y + "}";
        if (i < argc - 1)
          pointsJson += ",";
      }
      pointsJson += "]";
      msg = "{\"type\":\"ui_intent\",\"intent\":\"swipe_path\",\"layout\":\"" +
            layout + "\",\"points\":" + pointsJson + "}\n";
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
  int pr = poll(&pfd, 1, 100);
  if (pr < 0) {
    perror("poll");
  } else if (pr > 0) {
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
