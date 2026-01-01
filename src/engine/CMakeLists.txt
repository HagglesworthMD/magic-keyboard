# Fcitx5 Engine Addon

add_library(magickeyboard-engine MODULE
    magickeyboard.cpp
    swipe_engine.cpp
    shark2.cpp
    settings.cpp
    user_data.cpp
    lexicon/Trie.cpp
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

# Set output name WITH 'lib' prefix (Fcitx5 convention)
set_target_properties(magickeyboard-engine PROPERTIES
    PREFIX "lib"
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
    ${CMAKE_CURRENT_BINARY_DIR}/addon-magickeyboard.conf
    @ONLY
)

# Install targets
install(TARGETS magickeyboard-engine
    LIBRARY DESTINATION ${FCITX5_ADDON_LIB_DIR}
)

# Install inputmethod configuration (tells fcitx5 about our IM)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/magickeyboard.conf
    DESTINATION ${FCITX5_IM_CONF_DIR}
)

# Install addon configuration (tells fcitx5 how to load our addon)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/addon-magickeyboard.conf
    DESTINATION ${FCITX5_ADDON_CONF_DIR}
    RENAME magickeyboard.conf
)
