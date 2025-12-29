# Gesture Agent - Header-only module
# No compilation needed, just install headers

set(GESTURE_HEADERS
    gesture_agent.h
)

# Install headers for other modules to use
install(FILES ${GESTURE_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/magic-keyboard/gesture
)

# Create interface library for header-only usage
add_library(magicgesture INTERFACE)
target_include_directories(magicgesture INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
