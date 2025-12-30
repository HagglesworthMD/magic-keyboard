#!/bin/bash
# Magic Keyboard - Steam Deck Uninstallation Script
# This script completely removes Magic Keyboard from your system

set -e  # Exit on any error

BOLD='\033[1m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo ""
echo -e "${BOLD}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║       Magic Keyboard - Uninstallation Script                 ║${NC}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Confirm uninstallation
read -p "Are you sure you want to uninstall Magic Keyboard? (y/N): " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo -e "${YELLOW}Uninstallation cancelled${NC}"
    exit 0
fi

echo ""

# Step 1: Stop and disable services
echo -e "${BOLD}[1/4] Stopping services...${NC}"
if systemctl --user is-active --quiet magickeyboard-ui 2>/dev/null; then
    systemctl --user stop magickeyboard-ui
fi
if systemctl --user is-enabled --quiet magickeyboard-ui 2>/dev/null; then
    systemctl --user disable magickeyboard-ui
fi
pkill -f magickeyboard-ui || true
pkill fcitx5 || true
echo -e "${GREEN}✓ Services stopped${NC}"
echo ""

# Step 2: Remove user configuration files
echo -e "${BOLD}[2/4] Removing user configuration...${NC}"
rm -f ~/.config/systemd/user/magickeyboard-ui.service
rm -f ~/.config/autostart/magickeyboard-ui.desktop
rm -rf ~/.config/magic-keyboard/
rm -f ~/.config/plasma-workspace/env/fcitx5-env.sh
systemctl --user daemon-reload
echo -e "${GREEN}✓ User configuration removed${NC}"
echo ""

# Step 3: Remove system files
echo -e "${BOLD}[3/4] Removing system files...${NC}"
if command -v steamos-readonly &> /dev/null; then
    sudo steamos-readonly disable
fi

sudo rm -f /usr/local/lib/fcitx5/libmagickeyboard.so
sudo rm -f /usr/local/share/fcitx5/addon/magickeyboard.conf
sudo rm -f /usr/local/share/fcitx5/inputmethod/magickeyboard.conf
sudo rm -f /usr/local/bin/magickeyboard-ui
sudo rm -f /usr/local/bin/magickeyboardctl
sudo rm -f /usr/local/bin/magic-keyboard-verify
sudo rm -rf /usr/local/share/magic-keyboard/
sudo rm -f /usr/local/share/applications/magickeyboard*.desktop

# Also check /usr in case it was installed there
sudo rm -f /usr/lib/x86_64-linux-gnu/fcitx5/libmagickeyboard.so
sudo rm -f /usr/lib/fcitx5/libmagickeyboard.so
sudo rm -f /usr/share/fcitx5/addon/magickeyboard.conf
sudo rm -f /usr/share/fcitx5/inputmethod/magickeyboard.conf
sudo rm -f /usr/bin/magickeyboard-ui
sudo rm -f /usr/bin/magickeyboardctl
sudo rm -f /usr/bin/magic-keyboard-verify
sudo rm -f /usr/share/applications/magickeyboard*.desktop

echo -e "${GREEN}✓ System files removed${NC}"
echo ""

# Step 4: Clean up Flatpak overrides (optional)
echo -e "${BOLD}[4/4] Flatpak environment cleanup...${NC}"
read -p "Remove Fcitx5 environment from Flatpak apps? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    if command -v flatpak &> /dev/null; then
        flatpak override --user --unset-env=GTK_IM_MODULE
        flatpak override --user --unset-env=QT_IM_MODULE
        flatpak override --user --unset-env=XMODIFIERS
        echo -e "${GREEN}✓ Flatpak environment cleared${NC}"
    fi
else
    echo -e "${YELLOW}⚠ Keeping Flatpak Fcitx5 configuration${NC}"
fi

echo ""
echo -e "${BOLD}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║            Magic Keyboard Uninstalled Successfully           ║${NC}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${BOLD}Next steps:${NC}"
echo ""
echo "1. Remove 'Magic Keyboard' from System Settings → Input Method"
echo "2. Log out and log back in (or reboot)"
echo ""
echo "Thank you for trying Magic Keyboard!"
echo ""
