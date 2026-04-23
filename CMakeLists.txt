cmake_minimum_required(VERSION 3.16)

project(lan_file_transfer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(lan_transfer_lib
    src/app/file_receiver.cpp
    src/app/file_sender.cpp
    src/common/file_utils.cpp
    src/common/protocol.cpp
    src/config/settings.cpp
    src/discovery/connection_code.cpp
    src/discovery/code_lookup_service.cpp
    src/discovery/device_discovery.cpp
    src/discovery/presence_announcer.cpp
    src/pairing/pair_store.cpp
    src/pairing/pairing_manager.cpp
    src/security/path_sanitizer.cpp
    src/security/rate_limiter.cpp
    src/security/security_manager.cpp
    src/net/socket.cpp
)

target_include_directories(lan_transfer_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(WIN32)
    target_link_libraries(lan_transfer_lib PUBLIC ws2_32)
endif()

add_executable(lan_transfer
    src/main.cpp
)

target_link_libraries(lan_transfer PRIVATE lan_transfer_lib)
