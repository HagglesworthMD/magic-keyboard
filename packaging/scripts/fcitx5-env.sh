#!/bin/sh
# Fcitx5 environment setup for KDE Plasma
# Place this file at: ~/.config/plasma-workspace/env/fcitx5-env.sh
# Make executable: chmod +x ~/.config/plasma-workspace/env/fcitx5-env.sh
#
# This ensures all applications launched in the Plasma session
# know to use Fcitx5 for input method handling.

export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx

# For SDL2 applications (games, emulators)
export SDL_IM_MODULE=fcitx

# Uncomment for debugging Fcitx5 issues:
# export FCITX_DEBUG=1
