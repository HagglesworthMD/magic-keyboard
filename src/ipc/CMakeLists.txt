# IPC Protocol Library (header-only for now)

add_library(magickeyboard-ipc INTERFACE)

target_include_directories(magickeyboard-ipc INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
