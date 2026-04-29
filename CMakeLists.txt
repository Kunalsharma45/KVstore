cmake_minimum_required(VERSION 3.14)
project(utrade_kvstore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add headers directory (current dir)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Executable
add_executable(kvstore
    main.cpp
    store.cpp
    server.cpp
)

# Link against Winsock on Windows
if(WIN32)
    target_link_libraries(kvstore ws2_32)
endif()
