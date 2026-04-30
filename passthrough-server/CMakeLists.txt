cmake_minimum_required(VERSION 3.15)
project(MoonlightPassthroughServer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared protocol header (same as client)
set(PROTOCOL_HEADER ${CMAKE_SOURCE_DIR}/../app/streaming/passthrough/protocol.h)

add_executable(mlpt-server
    src/main.cpp
    src/server.h
    src/server.cpp
    src/vhci_manager.h
    src/vhci_manager.cpp
    src/systray.h
    src/systray.cpp
)

target_compile_definitions(mlpt-server PRIVATE UNICODE _UNICODE)

# Include the shared protocol header directory
target_include_directories(mlpt-server PRIVATE
    ${CMAKE_SOURCE_DIR}/../app/streaming/passthrough
)

# Windows-specific: link Winsock2 and SetupAPI
if(WIN32)
    target_link_libraries(mlpt-server PRIVATE ws2_32 setupapi shell32 advapi32)
    target_compile_definitions(mlpt-server PRIVATE
        _WIN32_WINNT=0x0A00
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# Install target
install(TARGETS mlpt-server RUNTIME DESTINATION bin)
