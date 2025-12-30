#!/bin/bash
# Magic Keyboard - Start Script
# Starts Fcitx5 and Magic Keyboard UI
# Can be run after installation to immediately start using Magic Keyboard

BOLD='\033[1m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${BOLD}Starting Magic Keyboard...${NC}"

# Set environment variables for this session
export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx
export SDL_IM_MODULE=fcitx

# Ensure DISPLAY is set (for Desktop Mode)
export DISPLAY="${DISPLAY:-:0}"

# Ensure XDG_RUNTIME_DIR is set
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

# Kill any existing instances first
pkill -f magickeyboard-ui 2>/dev/null || true

# Start or restart Fcitx5
if pgrep -x fcitx5 > /dev/null; then
    echo -e "  Restarting Fcitx5..."
    pkill -x fcitx5 || true
    sleep 1
fi

echo -e "  Starting Fcitx5..."
fcitx5 -d --replace 2>/dev/null &
sleep 2

# Verify Fcitx5 is running
if ! pgrep -x fcitx5 > /dev/null; then
    echo -e "${RED}✗ Failed to start Fcitx5${NC}"
    echo -e "  Try running: fcitx5 -d"
    exit 1
fi
echo -e "${GREEN}  ✓ Fcitx5 running${NC}"

# Reload Fcitx5 configuration to pick up Magic Keyboard
if command -v fcitx5-remote &> /dev/null; then
    fcitx5-remote -r 2>/dev/null || true
    sleep 1
fi

# Start Magic Keyboard UI via systemd if available
if systemctl --user is-enabled magickeyboard-ui 2>/dev/null; then
    echo -e "  Starting Magic Keyboard UI via systemd..."
    systemctl --user restart magickeyboard-ui
    sleep 1

    if systemctl --user is-active --quiet magickeyboard-ui 2>/dev/null; then
        echo -e "${GREEN}  ✓ Magic Keyboard UI running (systemd)${NC}"
    else
        echo -e "${YELLOW}  ⚠ Systemd service failed, starting directly...${NC}"
        /usr/local/bin/magickeyboard-ui &
        sleep 1
    fi
else
    # Start UI directly
    echo -e "  Starting Magic Keyboard UI..."
    if [[ -x /usr/local/bin/magickeyboard-ui ]]; then
        /usr/local/bin/magickeyboard-ui &
        sleep 1
        echo -e "${GREEN}  ✓ Magic Keyboard UI running${NC}"
    else
        echo -e "${RED}✗ magickeyboard-ui not found at /usr/local/bin/magickeyboard-ui${NC}"
        exit 1
    fi
fi

# Verify everything is running
echo ""
echo -e "${BOLD}Verification:${NC}"

if pgrep -x fcitx5 > /dev/null; then
    echo -e "${GREEN}  ✓ Fcitx5 is running${NC}"
else
    echo -e "${RED}  ✗ Fcitx5 is NOT running${NC}"
fi

if pgrep -f magickeyboard-ui > /dev/null; then
    echo -e "${GREEN}  ✓ Magic Keyboard UI is running${NC}"
else
    echo -e "${RED}  ✗ Magic Keyboard UI is NOT running${NC}"
fi

# Check if socket exists (IPC communication)
if [[ -S "${XDG_RUNTIME_DIR}/magic-keyboard.sock" ]]; then
    echo -e "${GREEN}  ✓ IPC socket created${NC}"
else
    echo -e "${YELLOW}  ⚠ IPC socket not yet created (may take a moment)${NC}"
fi

echo ""
echo -e "${GREEN}${BOLD}Magic Keyboard is ready!${NC}"
echo ""
echo -e "  • Click in any text field - keyboard should appear automatically"
echo -e "  • Or run: ${BOLD}magickeyboardctl toggle${NC} to show/hide manually"
echo -e "  • Press ${BOLD}Ctrl+Space${NC} to switch between keyboard/direct input"
echo ""
