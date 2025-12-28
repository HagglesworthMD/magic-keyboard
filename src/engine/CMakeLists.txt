# Fcitx5 Engine Addon

add_library(magickeyboard-engine MODULE
    magickeyboard.cpp
)

target_link_libraries(magickeyboard-engine
    PRIVATE
        Fcitx5::Core
        Fcitx5::Config
        magickeyboard-ipc
)

target_include_directories(magickeyboard-engine
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Set output name without 'lib' prefix
set_target_properties(magickeyboard-engine PROPERTIES
    PREFIX ""
    OUTPUT_NAME "magickeyboard"
)

# Generate addon configuration files
configure_file(
    magickeyboard.conf.in
    ${CMAKE_CURRENT_BINARY_DIR}/magickeyboard.conf
    @ONLY
)

configure_file(
    magickeyboard-addon.conf.in
    ${CMAKE_CURRENT_BINARY_DIR}/magickeyboard-addon.conf
    @ONLY
)

# Install targets
install(TARGETS magickeyboard-engine
    LIBRARY DESTINATION ${FCITX5_ADDON_LIB_DIR}
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/magickeyboard.conf
    DESTINATION ${FCITX5_ADDON_INSTALL_DIR}/inputmethod
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/magickeyboard-addon.conf
    DESTINATION ${FCITX5_ADDON_INSTALL_DIR}/addon
)
