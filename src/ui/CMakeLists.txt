# Keyboard UI (Qt6/QML)

qt_add_executable(magickeyboard-ui
    main.cpp
)

qt_add_qml_module(magickeyboard-ui
    URI MagicKeyboard
    VERSION 1.0
    QML_FILES
        KeyboardWindow.qml
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
