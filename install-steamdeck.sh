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

# Also install to user-local bin to avoid PATH conflicts
# This ensures ~/.local/bin/ has the SAME binary as /usr/local/bin/
mkdir -p ~/.local/bin
cp build/bin/magickeyboard-ui ~/.local/bin/
cp build/bin/magickeyboardctl ~/.local/bin/
chmod +x ~/.local/bin/magickeyboard-ui ~/.local/bin/magickeyboardctl
echo -e "${GREEN}✓ Installation complete (system + user-local)${NC}"
echo ""

# Step 6: Post-installation setup
echo -e "${BOLD}[6/7] Setting up environment...${NC}"

# Create plasma-workspace env directory
mkdir -p ~/.config/plasma-workspace/env

# Copy and make executable the fcitx5 environment script
if [[ -f /usr/local/share/magic-keyboard/fcitx5-env.sh ]]; then
    cp /usr/local/share/magic-keyboard/fcitx5-env.sh ~/.config/plasma-workspace/env/
    chmod +x ~/.config/plasma-workspace/env/fcitx5-env.sh
    echo -e "${GREEN}✓ Environment script installed${NC}"
fi

# Set up systemd user service - create directly with correct path
mkdir -p ~/.config/systemd/user
cat > ~/.config/systemd/user/magickeyboard-ui.service << 'SERVICE_EOF'
[Unit]
Description=Magic Keyboard UI
After=graphical-session.target
PartOf=graphical-session.target
StartLimitIntervalSec=30
StartLimitBurst=3

[Service]
Type=simple
# Use ~/.local/bin to ensure we always use the user-installed binary
ExecStart=%h/.local/bin/magickeyboard-ui
Restart=on-failure
RestartSec=2

# SteamOS desktop session is X11 here (DISPLAY=:0, no WAYLAND_DISPLAY)
Environment=DISPLAY=:0
Environment=XDG_RUNTIME_DIR=%t
Environment=QT_QPA_PLATFORM=xcb

# Optional: less crash-noise if something goes wrong
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=graphical-session.target
SERVICE_EOF
systemctl --user daemon-reload
systemctl --user enable magickeyboard-ui
echo -e "${GREEN}✓ Systemd service configured${NC}"

# Configure Flatpak apps
if command -v flatpak &> /dev/null; then
    echo "Configuring Flatpak environment..."
    flatpak override --user --env=GTK_IM_MODULE=fcitx
    flatpak override --user --env=QT_IM_MODULE=fcitx
    flatpak override --user --env=XMODIFIERS=@im=fcitx
    echo -e "${GREEN}✓ Flatpak configured${NC}"
fi

echo ""

# Step 7: Configure input method (NEW - automatic!)
echo -e "${BOLD}[7/7] Configuring Magic Keyboard as default input method...${NC}"

# Run the configuration script
if [[ -f /usr/local/share/magic-keyboard/configure-input-method.sh ]]; then
    bash /usr/local/share/magic-keyboard/configure-input-method.sh
else
    # Fallback: inline configuration if script not installed yet
    FCITX5_CONFIG_DIR="${HOME}/.config/fcitx5"
    mkdir -p "${FCITX5_CONFIG_DIR}"

    # Create Fcitx5 profile with Magic Keyboard
    cat > "${FCITX5_CONFIG_DIR}/profile" << 'PROFILE_EOF'
[Groups/0]
Name=Default
Default Layout=us
DefaultIM=magic-keyboard

[Groups/0/Items/0]
Name=keyboard-us
Layout=

[Groups/0/Items/1]
Name=magic-keyboard
Layout=

[GroupOrder]
0=Default
PROFILE_EOF

    echo -e "${GREEN}✓ Created Fcitx5 profile with Magic Keyboard${NC}"

    # Create symlinks for addon discovery
    mkdir -p "${HOME}/.local/share/fcitx5/addon"
    mkdir -p "${HOME}/.local/share/fcitx5/inputmethod"

    ln -sf /usr/local/share/fcitx5/addon/magickeyboard.conf \
        "${HOME}/.local/share/fcitx5/addon/magickeyboard.conf" 2>/dev/null || true
    ln -sf /usr/local/share/fcitx5/inputmethod/magickeyboard.conf \
        "${HOME}/.local/share/fcitx5/inputmethod/magickeyboard.conf" 2>/dev/null || true
fi

echo -e "${GREEN}✓ Input method configured automatically${NC}"
echo ""

echo -e "${BOLD}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║               Installation Successful!                        ║${NC}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Ask if user wants to start Magic Keyboard now
echo -e "${BOLD}Would you like to start Magic Keyboard now?${NC}"
echo "This will start Fcitx5 and the keyboard UI immediately."
echo ""
read -p "Start now? [Y/n]: " START_NOW
START_NOW=${START_NOW:-Y}

if [[ "${START_NOW}" =~ ^[Yy]$ ]]; then
    echo ""
    if [[ -f /usr/local/share/magic-keyboard/start-magic-keyboard.sh ]]; then
        bash /usr/local/share/magic-keyboard/start-magic-keyboard.sh
    else
        # Inline startup
        echo -e "${BOLD}Starting Magic Keyboard...${NC}"

        # Set environment for this session
        export GTK_IM_MODULE=fcitx
        export QT_IM_MODULE=fcitx
        export XMODIFIERS=@im=fcitx
        export DISPLAY="${DISPLAY:-:0}"

        # Start Fcitx5
        pkill -x fcitx5 2>/dev/null || true
        sleep 1
        fcitx5 -d --replace 2>/dev/null &
        sleep 2

        # Start UI
        systemctl --user restart magickeyboard-ui 2>/dev/null || \
            /usr/local/bin/magickeyboard-ui &

        echo -e "${GREEN}✓ Magic Keyboard started!${NC}"
    fi
    echo ""
    echo -e "${GREEN}Magic Keyboard is now running!${NC}"
    echo "  • Click in any text field - keyboard should appear"
    echo "  • Run 'magickeyboardctl toggle' to show/hide manually"
    echo ""
else
    echo ""
    echo -e "${BOLD}To start Magic Keyboard later:${NC}"
    echo "  Option 1: Log out and log back in"
    echo "  Option 2: Run: ${BOLD}start-magic-keyboard${NC}"
    echo ""
fi

echo -e "${BOLD}Useful commands:${NC}"
echo "  ${BOLD}magickeyboardctl toggle${NC}  - Show/hide keyboard"
echo "  ${BOLD}magickeyboardctl show${NC}    - Show keyboard"
echo "  ${BOLD}magickeyboardctl hide${NC}    - Hide keyboard"
echo "  ${BOLD}magic-keyboard-verify${NC}    - Check installation health"
echo "  ${BOLD}start-magic-keyboard${NC}     - Start/restart Magic Keyboard"
echo ""
