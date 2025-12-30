#!/bin/bash
# Magic Keyboard - Steam Deck Installation Script
# This script removes the old version and installs the new version

set -e  # Exit on any error

BOLD='\033[1m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo ""
echo -e "${BOLD}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║        Magic Keyboard - Steam Deck Installation              ║${NC}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Check if running on Steam Deck
if [[ ! -f /etc/os-release ]] || ! grep -q "steamdeck" /etc/os-release 2>/dev/null; then
    echo -e "${YELLOW}⚠ Warning: This doesn't appear to be a Steam Deck${NC}"
    echo -e "${YELLOW}  Continuing anyway...${NC}"
    echo ""
fi

# Step 1: Disable read-only filesystem (SteamOS)
echo -e "${BOLD}[1/6] Checking filesystem write access...${NC}"
if command -v steamos-readonly &> /dev/null; then
    echo "Disabling read-only filesystem..."
    sudo steamos-readonly disable
    echo -e "${GREEN}✓ Filesystem is now writable${NC}"
else
    echo -e "${YELLOW}⚠ Not SteamOS or steamos-readonly not available, skipping${NC}"
fi
echo ""

# Step 2: Stop running services
echo -e "${BOLD}[2/6] Stopping existing Magic Keyboard services...${NC}"
if systemctl --user is-active --quiet magickeyboard-ui 2>/dev/null; then
    echo "Stopping magickeyboard-ui service..."
    systemctl --user stop magickeyboard-ui || true
fi

# Kill any running UI processes
if pgrep -f magickeyboard-ui > /dev/null; then
    echo "Stopping running UI processes..."
    pkill -f magickeyboard-ui || true
fi

# Restart fcitx5 to unload the old addon
if pgrep fcitx5 > /dev/null; then
    echo "Restarting Fcitx5..."
    pkill fcitx5 || true
    sleep 2
fi
echo -e "${GREEN}✓ Services stopped${NC}"
echo ""

# Step 3: Remove old installation
echo -e "${BOLD}[3/6] Removing old installation...${NC}"
echo "Removing old binaries and libraries..."
sudo rm -f /usr/local/lib/fcitx5/libmagickeyboard.so
sudo rm -f /usr/local/share/fcitx5/addon/magickeyboard.conf
sudo rm -f /usr/local/share/fcitx5/inputmethod/magickeyboard.conf
sudo rm -f /usr/local/bin/magickeyboard-ui
sudo rm -f /usr/local/bin/magickeyboardctl
sudo rm -f /usr/local/bin/magic-keyboard-verify
sudo rm -rf /usr/local/share/magic-keyboard/
sudo rm -f /usr/share/applications/magickeyboard*.desktop
sudo rm -f /usr/local/share/applications/magickeyboard*.desktop

# Also check /usr in case it was installed there before
sudo rm -f /usr/lib/x86_64-linux-gnu/fcitx5/libmagickeyboard.so
sudo rm -f /usr/lib/fcitx5/libmagickeyboard.so
sudo rm -f /usr/share/fcitx5/addon/magickeyboard.conf
sudo rm -f /usr/share/fcitx5/inputmethod/magickeyboard.conf
sudo rm -f /usr/bin/magickeyboard-ui
sudo rm -f /usr/bin/magickeyboardctl
sudo rm -f /usr/bin/magic-keyboard-verify

echo -e "${GREEN}✓ Old installation removed${NC}"
echo ""

# Step 4: Build the new version
echo -e "${BOLD}[4/6] Building new version...${NC}"
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release
cmake --build build
echo -e "${GREEN}✓ Build complete${NC}"
echo ""

# Step 5: Install the new version
echo -e "${BOLD}[5/6] Installing new version...${NC}"
sudo cmake --install build
echo -e "${GREEN}✓ Installation complete${NC}"
echo ""

# Step 6: Post-installation setup
echo -e "${BOLD}[6/6] Setting up environment...${NC}"

# Create plasma-workspace env directory
mkdir -p ~/.config/plasma-workspace/env

# Copy and make executable the fcitx5 environment script
if [[ -f /usr/local/share/magic-keyboard/fcitx5-env.sh ]]; then
    cp /usr/local/share/magic-keyboard/fcitx5-env.sh ~/.config/plasma-workspace/env/
    chmod +x ~/.config/plasma-workspace/env/fcitx5-env.sh
    echo -e "${GREEN}✓ Environment script installed${NC}"
fi

# Set up systemd user service
mkdir -p ~/.config/systemd/user
if [[ -f /usr/local/share/magic-keyboard/magickeyboard-ui.service ]]; then
    cp /usr/local/share/magic-keyboard/magickeyboard-ui.service ~/.config/systemd/user/
    systemctl --user daemon-reload
    systemctl --user enable magickeyboard-ui
    echo -e "${GREEN}✓ Systemd service enabled${NC}"
fi

# Configure Flatpak apps
if command -v flatpak &> /dev/null; then
    echo "Configuring Flatpak environment..."
    flatpak override --user --env=GTK_IM_MODULE=fcitx
    flatpak override --user --env=QT_IM_MODULE=fcitx
    flatpak override --user --env=XMODIFIERS=@im=fcitx
    echo -e "${GREEN}✓ Flatpak configured${NC}"
fi

echo ""
echo -e "${BOLD}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║               Installation Successful! 🎉                     ║${NC}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${BOLD}IMPORTANT: Complete these final steps:${NC}"
echo ""
echo "1. Add 'Magic Keyboard' in System Settings:"
echo "   ${BOLD}System Settings → Input Method → Add 'Magic Keyboard'${NC}"
echo ""
echo "2. Log out and log back in (or reboot) for changes to take effect"
echo ""
echo "3. After logging back in, verify the installation:"
echo "   ${BOLD}magic-keyboard-verify${NC}"
echo ""
echo "4. To manually control the keyboard:"
echo "   ${BOLD}magickeyboardctl toggle${NC}"
echo ""
echo "For detailed instructions:"
echo "   /usr/local/share/doc/magic-keyboard/post-install.txt"
echo ""
