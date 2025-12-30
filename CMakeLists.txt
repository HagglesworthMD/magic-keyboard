cmake_minimum_required(VERSION 3.20)

project(magic-keyboard
    VERSION 0.1.0
    DESCRIPTION "SteamOS Desktop Mode On-Screen Keyboard"
    LANGUAGES CXX
)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Export compile commands for IDE/tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(PkgConfig REQUIRED)

# Fcitx5
find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Config REQUIRED)
find_package(Fcitx5Utils REQUIRED)

# Qt6 for UI
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Network)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Installation directories
include(GNUInstallDirs)

# Fcitx5 addon install paths
# Try pkg-config first; fallback to GNUInstallDirs-based paths
# CRITICAL: Never allow empty paths that resolve to root filesystem
pkg_get_variable(_FCITX5_PKGDATADIR Fcitx5Utils pkgdatadir)

if(_FCITX5_PKGDATADIR AND NOT _FCITX5_PKGDATADIR STREQUAL "")
    # pkg-config gave us a valid path
    set(FCITX5_ADDON_CONF_DIR "${_FCITX5_PKGDATADIR}/addon")
    set(FCITX5_IM_CONF_DIR "${_FCITX5_PKGDATADIR}/inputmethod")
else()
    # Fallback: use standard GNUInstallDirs paths
    # CMAKE_INSTALL_DATAROOTDIR is typically "share"
    set(FCITX5_ADDON_CONF_DIR "${CMAKE_INSTALL_DATAROOTDIR}/fcitx5/addon")
    set(FCITX5_IM_CONF_DIR "${CMAKE_INSTALL_DATAROOTDIR}/fcitx5/inputmethod")
endif()

set(FCITX5_ADDON_LIB_DIR "${CMAKE_INSTALL_LIBDIR}/fcitx5")

# GUARD: Fail at configure time if paths would install to root
# This catches the bug where pkg-config returns empty and fallback is broken
if(FCITX5_ADDON_CONF_DIR MATCHES "^/addon$" OR FCITX5_ADDON_CONF_DIR MATCHES "^addon$")
    message(FATAL_ERROR
        "FCITX5_ADDON_CONF_DIR resolved to '${FCITX5_ADDON_CONF_DIR}' which would install to root. "
        "Check pkg-config Fcitx5Utils and CMAKE_INSTALL_DATAROOTDIR.")
endif()
if(FCITX5_IM_CONF_DIR MATCHES "^/inputmethod$" OR FCITX5_IM_CONF_DIR MATCHES "^inputmethod$")
    message(FATAL_ERROR
        "FCITX5_IM_CONF_DIR resolved to '${FCITX5_IM_CONF_DIR}' which would install to root. "
        "Check pkg-config Fcitx5Utils and CMAKE_INSTALL_DATAROOTDIR.")
endif()

message(STATUS "Fcitx5 addon conf dir: ${FCITX5_ADDON_CONF_DIR}")
message(STATUS "Fcitx5 inputmethod dir: ${FCITX5_IM_CONF_DIR}")
message(STATUS "Fcitx5 addon lib dir: ${FCITX5_ADDON_LIB_DIR}")

# Data directory
set(MAGIC_KEYBOARD_DATA_DIR "${CMAKE_INSTALL_DATAROOTDIR}/magic-keyboard")

# Deployment Options
option(MAGICKEYBOARD_INSTALL_DESKTOP "Install system-wide desktop launcher" OFF)

# Subdirectories
add_subdirectory(src/ipc)
add_subdirectory(src/gesture)
add_subdirectory(src/engine)
add_subdirectory(src/ui)

# Install data files
install(DIRECTORY data/layouts data/dict data/themes
    DESTINATION ${MAGIC_KEYBOARD_DATA_DIR}
)

# Summary
message(STATUS "")
message(STATUS "Magic Keyboard v${PROJECT_VERSION}")
message(STATUS "  Fcitx5 addon: src/engine")
message(STATUS "  UI:           src/ui")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")

# -----------------------------------------------------------------------------
# Packaging and Integration
# -----------------------------------------------------------------------------

# Systemd Service
# Systemd user units:
# For user instances, systemd searches XDG data dirs (e.g. ~/.local/share/systemd/user)
# and ~/.config/systemd/user. It does NOT scan ~/.local/lib/systemd/user by default.
# So we install to share/systemd/user under the chosen prefix.
set(SYSTEMD_USER_UNIT_DIR "${CMAKE_INSTALL_DATAROOTDIR}/systemd/user" CACHE PATH "Installation directory for systemd user units")
configure_file(packaging/systemd/magickeyboard-ui.service.in magickeyboard-ui.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/magickeyboard-ui.service DESTINATION ${SYSTEMD_USER_UNIT_DIR})

# Install fcitx5 user unit (optional helper for dev/test workflow)
install(FILES packaging/systemd/fcitx5.service DESTINATION ${SYSTEMD_USER_UNIT_DIR})

# Install fcitx5 launcher script (escapes namespace sandboxing on SteamOS)
install(PROGRAMS packaging/scripts/fcitx5-launch.sh DESTINATION ${CMAKE_INSTALL_BINDIR})

# Install Magic Keyboard helper scripts
# start-magic-keyboard: User-facing command to start/restart Magic Keyboard
install(PROGRAMS packaging/scripts/start-magic-keyboard.sh
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME start-magic-keyboard)

# Configure input method script (called by installer, also available to users)
install(PROGRAMS packaging/scripts/configure-input-method.sh
    DESTINATION ${MAGIC_KEYBOARD_DATA_DIR})

# Also install start script to data dir for install script to find
install(PROGRAMS packaging/scripts/start-magic-keyboard.sh
    DESTINATION ${MAGIC_KEYBOARD_DATA_DIR})

# Install fcitx5 environment setup script (sets IM module environment variables)
install(PROGRAMS packaging/scripts/fcitx5-env.sh
    DESTINATION ${MAGIC_KEYBOARD_DATA_DIR})

# Desktop Entries
if(MAGICKEYBOARD_INSTALL_DESKTOP)
    configure_file(packaging/applications/magickeyboard.desktop.in magickeyboard.desktop @ONLY)
    configure_file(packaging/applications/magickeyboard-show.desktop.in magickeyboard-show.desktop @ONLY)
    configure_file(packaging/applications/magickeyboard-hide.desktop.in magickeyboard-hide.desktop @ONLY)
    configure_file(packaging/applications/magickeyboard-toggle.desktop.in magickeyboard-toggle.desktop @ONLY)
    
    install(FILES 
        ${CMAKE_CURRENT_BINARY_DIR}/magickeyboard.desktop
        ${CMAKE_CURRENT_BINARY_DIR}/magickeyboard-show.desktop
        ${CMAKE_CURRENT_BINARY_DIR}/magickeyboard-hide.desktop
        ${CMAKE_CURRENT_BINARY_DIR}/magickeyboard-toggle.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    )
endif()
