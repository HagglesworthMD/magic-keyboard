#pragma once

/**
 * Magic Keyboard IPC Protocol
 * 
 * Communication between magickeyboard-engine (Fcitx5 addon) and
 * magickeyboard-ui (Qt6/QML process).
 * 
 * Transport: Unix Domain Socket
 * Format: JSON Lines (one JSON object per line, newline-delimited)
 * 
 * This is intentionally simple for v0.1. May migrate to protobuf/capnproto
 * if performance becomes an issue.
 */

#include <string>
#include <string_view>

namespace magickeyboard::ipc {

// Socket path (in XDG_RUNTIME_DIR)
constexpr std::string_view SOCKET_NAME = "magic-keyboard.sock";

/**
 * Message Types
 * 
 * UI → Engine:
 *   key        - Single key press
 *   swipe_start- Swipe gesture started
 *   swipe_move - Swipe position update (high-rate)
 *   swipe_end  - Swipe gesture ended
 *   action     - Special action (copy, paste, etc.)
 * 
 * Engine → UI:
 *   show       - Display keyboard window
 *   hide       - Hide keyboard window
 *   candidates - Update candidate word list
 *   preedit    - Update preedit string display
 */

// UI → Engine message types
namespace msg_type {
    constexpr std::string_view KEY = "key";
    constexpr std::string_view SWIPE_START = "swipe_start";
    constexpr std::string_view SWIPE_MOVE = "swipe_move";
    constexpr std::string_view SWIPE_END = "swipe_end";
    constexpr std::string_view ACTION = "action";
    constexpr std::string_view CANDIDATE_SELECT = "candidate_select";
}

// Engine → UI message types
namespace cmd_type {
    constexpr std::string_view SHOW = "show";
    constexpr std::string_view HIDE = "hide";
    constexpr std::string_view CANDIDATES = "candidates";
    constexpr std::string_view PREEDIT = "preedit";
}

// Action names
namespace action {
    constexpr std::string_view COPY = "copy";
    constexpr std::string_view PASTE = "paste";
    constexpr std::string_view CUT = "cut";
    constexpr std::string_view SELECT_ALL = "selectall";
}

/**
 * Example messages (JSON Lines format):
 * 
 * UI → Engine:
 *   {"type":"key","key":"a","modifiers":[]}
 *   {"type":"key","key":"backspace","modifiers":[]}
 *   {"type":"key","key":"a","modifiers":["shift"]}
 *   {"type":"swipe_start","x":100,"y":200,"time":1234567890}
 *   {"type":"swipe_move","x":110,"y":195,"time":1234567898}
 *   {"type":"swipe_end","time":1234568000}
 *   {"type":"action","action":"paste"}
 *   {"type":"candidate_select","index":2}
 * 
 * Engine → UI:
 *   {"type":"show"}
 *   {"type":"hide"}
 *   {"type":"candidates","words":["hello","help","held"]}
 *   {"type":"preedit","text":"hel","cursor":3}
 */

// Helper: get socket path
inline std::string getSocketPath() {
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir) {
        return std::string(runtime_dir) + "/" + std::string(SOCKET_NAME);
    }
    // Fallback to /tmp (less secure but works)
    return "/tmp/" + std::string(SOCKET_NAME);
}

} // namespace magickeyboard::ipc
