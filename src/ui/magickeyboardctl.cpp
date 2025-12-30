#include "protocol.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

static void usage() {
  std::cerr << "Usage: magickeyboardctl [show|hide|toggle|kill-ui|ui-intent]"
            << std::endl;
}

static void usageUiIntent() {
  std::cerr << "Usage: magickeyboardctl ui-intent [--delay-ms N] "
               "<key|action|swipe> <value> [...]"
            << std::endl;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    usage();
    return 1;
  }

  std::string cmd = argv[1];
  std::string msg;
  int delayMs = 0;

  // Emergency kill command - does NOT use socket, sends SIGUSR1 to UI process
  if (cmd == "kill-ui") {
    // Use pkill to find and signal the UI process by exact name
    int ret = std::system("pkill -USR1 -x magickeyboard-ui");
    if (ret == 0) {
      std::cout << "Emergency UI kill sent - focus should be restored"
                << std::endl;
      return 0;
    } else {
      std::cerr << "Failed to find UI process (magickeyboard-ui)" << std::endl;
      return 1;
    }
  }

  if (cmd == "show") {
    msg = "{\"type\":\"ui_show\"}\n";
  } else if (cmd == "hide") {
    msg = "{\"type\":\"ui_hide\"}\n";
  } else if (cmd == "toggle") {
    msg = "{\"type\":\"ui_toggle\"}\n";
  } else if (cmd == "ui-intent") {
    int argOffset = 0;
    // Optional: --delay-ms N (must appear before intent type)
    if (argc >= 4 && std::string(argv[2]) == "--delay-ms") {
      if (argc < 5) {
        usageUiIntent();
        return 2;
      }
      try {
        delayMs = std::stoi(argv[3]);
        if (delayMs < 0) {
          throw std::out_of_range("negative");
        }
      } catch (...) {
        std::cerr << "Invalid value for --delay-ms: " << argv[3] << std::endl;
        return 2;
      }
      argOffset = 2; // consumes "--delay-ms" + N
    }

    if (argc < 3 + argOffset) {
      usageUiIntent();
      return 2;
    }

    std::string type = argv[2 + argOffset];
    if (type == "key" || type == "action") {
      if (argc < 4 + argOffset) {
        usageUiIntent();
        return 2;
      }
      std::string val = argv[3 + argOffset];
      msg = "{\"type\":\"ui_intent\",\"intent\":\"" + type + "\",\"value\":\"" +
            val + "\"}\n";
    } else if (type == "swipe") {
      if (argc < 4 + argOffset) {
        usageUiIntent();
        return 2;
      }
      std::string val = argv[3 + argOffset];
      std::string mag = (argc > 4 + argOffset) ? argv[4 + argOffset] : "1.0";
      msg = "{\"type\":\"ui_intent\",\"intent\":\"swipe\",\"dir\":\"" + val +
            "\",\"mag\":" + mag + "}\n";
    } else if (type == "swipe-path") {
      if (argc < 4 + argOffset) {
        usageUiIntent();
        return 2;
      }
      std::string layout = argv[3 + argOffset];
      std::string pointsJson = "[";
      for (int i = 4 + argOffset; i < argc; ++i) {
        std::string ptStr = argv[i];
        size_t comma = ptStr.find(',');
        if (comma == std::string::npos) {
          std::cerr << "Malformed point: " << ptStr << " (expected x,y)"
                    << std::endl;
          return 2;
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
      usageUiIntent();
      return 2;
    }
  } else {
    std::cerr << "Unknown command: " << cmd << std::endl;
    usage();
    return 1;
  }

  // Apply delay if requested (for focus-switching before send)
  if (delayMs > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
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

  // Wait for acknowledgment with 100ms timeout
  struct pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLIN;
  int pr = poll(&pfd, 1, 100);
  if (pr > 0) {
    char buf[128];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    (void)n;
  }

  close(fd);
  return 0;
}
