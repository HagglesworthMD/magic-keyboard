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

# Fcitx5 addon install path
pkg_get_variable(FCITX5_ADDON_INSTALL_DIR Fcitx5Utils pkgdatadir)
set(FCITX5_ADDON_LIB_DIR "${CMAKE_INSTALL_LIBDIR}/fcitx5")
message(STATUS "Fcitx5 addon data dir: ${FCITX5_ADDON_INSTALL_DIR}")
message(STATUS "Fcitx5 addon lib dir: ${FCITX5_ADDON_LIB_DIR}")

# Data directory
set(MAGIC_KEYBOARD_DATA_DIR "${CMAKE_INSTALL_DATADIR}/magic-keyboard")

# Subdirectories
add_subdirectory(src/ipc)
add_subdirectory(src/engine)
add_subdirectory(src/ui)

# Install data files
install(DIRECTORY data/layouts
    DESTINATION ${MAGIC_KEYBOARD_DATA_DIR}
)

# Summary
message(STATUS "")
message(STATUS "Magic Keyboard v${PROJECT_VERSION}")
message(STATUS "  Fcitx5 addon: src/engine")
message(STATUS "  UI:           src/ui")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
