#!/bin/bash
# Magic Keyboard - Input Method Configuration Script
# This script automatically configures Fcitx5 to use Magic Keyboard
# without requiring manual System Settings configuration.

set -e

BOLD='\033[1m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

FCITX5_CONFIG_DIR="${HOME}/.config/fcitx5"
FCITX5_PROFILE="${FCITX5_CONFIG_DIR}/profile"
FCITX5_CONFIG="${FCITX5_CONFIG_DIR}/config"

echo -e "${BOLD}Configuring Magic Keyboard as default input method...${NC}"

# Create Fcitx5 config directory
mkdir -p "${FCITX5_CONFIG_DIR}"

# Backup existing profile if it exists
if [[ -f "${FCITX5_PROFILE}" ]]; then
    cp "${FCITX5_PROFILE}" "${FCITX5_PROFILE}.backup.$(date +%Y%m%d%H%M%S)"
    echo -e "${YELLOW}  Backed up existing profile${NC}"
fi

# Create Fcitx5 profile with Magic Keyboard as the active input method
# Format: INI-style with groups for input method selection
cat > "${FCITX5_PROFILE}" << 'EOF'
[Groups/0]
# Default group with Magic Keyboard
Name=Default
Default Layout=us
# DefaultIM specifies the default input method when switching
DefaultIM=magic-keyboard

[Groups/0/Items/0]
# Standard US keyboard layout (for direct typing)
Name=keyboard-us
Layout=

[Groups/0/Items/1]
# Magic Keyboard - on-screen keyboard with swipe typing
Name=magic-keyboard
Layout=

[GroupOrder]
0=Default
EOF

echo -e "${GREEN}  ✓ Created Fcitx5 profile with Magic Keyboard${NC}"

# Create/update Fcitx5 main config for better integration
if [[ ! -f "${FCITX5_CONFIG}" ]]; then
    cat > "${FCITX5_CONFIG}" << 'EOF'
[Hotkey]
# Trigger input method popup (can also use Ctrl+Space)
TriggerKeys=
# Enumerate through input methods
EnumerateWithTriggerKeys=True
# Skip first input method when switching (since keyboard-us is first)
EnumerateSkipFirst=False

[Hotkey/TriggerKeys]
0=Control+space

[Hotkey/EnumerateForwardKeys]
0=Control+Shift+space

[Behavior]
# Share input state across all applications
ShareInputState=All
# Show preedit in application (important for Magic Keyboard)
PreeditEnabledByDefault=True
# Show input method info on switch
ShowInputMethodInformation=True
ShowInputMethodInformationWhenFocusIn=False
# Default page size for candidate lists
DefaultPageSize=5

[Behavior/DisabledAddons]
EOF
    echo -e "${GREEN}  ✓ Created Fcitx5 config${NC}"
fi

# Ensure the data directories exist for Fcitx5 to find our addon
mkdir -p "${HOME}/.local/share/fcitx5/addon"
mkdir -p "${HOME}/.local/share/fcitx5/inputmethod"

# If system-installed configs exist, create symlinks in user directories
# This helps Fcitx5 discover the addon more reliably
if [[ -f /usr/local/share/fcitx5/addon/magickeyboard.conf ]]; then
    ln -sf /usr/local/share/fcitx5/addon/magickeyboard.conf \
        "${HOME}/.local/share/fcitx5/addon/magickeyboard.conf" 2>/dev/null || true
fi

if [[ -f /usr/local/share/fcitx5/inputmethod/magickeyboard.conf ]]; then
    ln -sf /usr/local/share/fcitx5/inputmethod/magickeyboard.conf \
        "${HOME}/.local/share/fcitx5/inputmethod/magickeyboard.conf" 2>/dev/null || true
fi

echo -e "${GREEN}  ✓ Created user-local addon symlinks${NC}"

echo -e "${GREEN}✓ Input method configuration complete${NC}"
