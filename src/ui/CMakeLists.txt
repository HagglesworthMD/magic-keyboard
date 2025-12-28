# Keyboard UI (Qt6/QML)

# Required for Qt6 QML module metatype generation
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

qt_add_executable(magickeyboard-ui
    main.cpp
)

set_target_properties(magickeyboard-ui PROPERTIES
    AUTOMOC ON
)

qt_add_qml_module(magickeyboard-ui
    URI MagicKeyboard
    VERSION 1.0
    QML_FILES
        KeyboardWindow.qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/MagicKeyboard
)

target_link_libraries(magickeyboard-ui
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Network
        magickeyboard-ipc
)

# Install
install(TARGETS magickeyboard-ui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
